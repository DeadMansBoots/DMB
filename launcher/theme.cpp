/*
 * theme.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "theme.h"
#include "gamefont.h"

#include "../lib/CConfigHandler.h"
#include "../lib/GameButtonLettering.h"
#include "../lib/VCMIDirs.h"
#include "../lib/filesystem/Filesystem.h"
#include "../lib/texts/CGeneralTextHandler.h"
#include "../lib/vcmi_endian.h"

#include <QApplication>
#include <QIcon>
#include <QPalette>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QStyleFactory>
#include <QTimer>

#ifdef VCMI_WINDOWS
#include <windows.h>
#endif

namespace
{
/// whether windows get Windows' dark title bar: in the leather and dark looks
bool darkFrames = false;

/// the height every launcher button's gold bar is drawn at (wordlessBar), the game's own 40 px bars
/// scaled down to the launcher's own, smaller buttons; a carved word is scaled the same amount, so its
/// letters stay in the bar's own proportion instead of standing oversized on a shrunk bar
constexpr int BAR_HEIGHT = 28;

/// A window's title bar in Windows' dark frame (Windows 10 1809 and later), or back in its light one;
/// elsewhere the title bar is the window manager's
void frameWindow(QWidget * window)
{
#ifdef VCMI_WINDOWS
	using SetAttribute = HRESULT(WINAPI *)(HWND, DWORD, LPCVOID, DWORD);
	static const auto setAttribute = reinterpret_cast<SetAttribute>(
		reinterpret_cast<void *>(GetProcAddress(LoadLibraryW(L"dwmapi.dll"), "DwmSetWindowAttribute")));
	if(!setAttribute || !window || !window->isWindow())
		return;
	const BOOL dark = darkFrames ? TRUE : FALSE;
	const auto handle = reinterpret_cast<HWND>(window->winId());
	setAttribute(handle, 20, &dark, sizeof(dark)); // DWMWA_USE_IMMERSIVE_DARK_MODE
	// the frame repaints only when told it changed
	SetWindowPos(handle, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
#else
	(void)window;
#endif
}

/// DMB: carves every QPushButton under `window` into the game's gold-button lettering, or plain if the
/// look is not leather or dark; defined with the rest of that work, further down this file
void carveEveryButtonUnder(QWidget * window, bool active);
void applyCarvedButtonWord(QPushButton * button, bool active);

/// frames every window as it first shows, dialogs included, and carves every button's word into the
/// game's gold-button lettering (K's testing found the first cut of this missed the Settings page's
/// own category buttons, September 28th: they sit on a tab that is not the one shown first, so the
/// main window's own Show, the only signal read before this, came and went before they existed on a
/// visible page at all). A button not yet on a shown page still fires its own Show once it is (Qt
/// shows every descendant of a newly-current QStackedWidget page); read one event loop turn late
/// (QTimer::singleShot(0, ...)), since its layout, and so its width, is not real any earlier than that
/// either (found testing the very first buttons this same morning).
class FrameEveryWindow : public QObject
{
public:
	bool eventFilter(QObject * watched, QEvent * event) override
	{
		if(event->type() != QEvent::Show)
			return false;
		if(auto * widget = qobject_cast<QWidget *>(watched); widget && widget->isWindow())
			frameWindow(widget);
		if(auto * button = qobject_cast<QPushButton *>(watched))
		{
			QPointer<QPushButton> guarded(button);
			const bool active = darkFrames;
			QTimer::singleShot(0, [guarded, active]()
			{
				if(guarded)
					applyCarvedButtonWord(guarded, active);
			});
		}
		return false;
	}
};

/// A Heroes III PCX (8-bit with its palette at the end, or 24-bit), read as the map editor reads them
QImage readH3Pcx(const ui8 * pcx, size_t size)
{
	if(size < 12)
		return {};
	const ui32 bytes = read_le_u32(pcx);
	const ui32 width = read_le_u32(pcx + 4);
	const ui32 height = read_le_u32(pcx + 8);
	if(bytes == width * height && size >= 12 + bytes + 256 * 3)
	{
		QImage image(pcx + 12, width, height, width, QImage::Format_Indexed8);
		QVector<QRgb> colours;
		const ui8 * palette = pcx + size - 256 * 3;
		for(int i = 0; i < 256; ++i)
			colours.append(qRgb(palette[3 * i], palette[3 * i + 1], palette[3 * i + 2]));
		image.setColorTable(colours);
		return image.copy(); // the caller's buffer goes away
	}
	if(bytes == width * height * 3 && size >= 12 + bytes)
		return QImage(pcx + 12, width, height, width * 3, QImage::Format_BGR888).copy();
	return {};
}

/// The leather Heroes III fills its windows with, from the player's own game files; null before an import
QPixmap playerLeather()
{
	const ResourcePath path("DATA/DIBOXBCK", EResType::IMAGE);
	if(!CResourceHandler::get()->existsResource(path))
		return {};
	const auto data = CResourceHandler::get()->load(path)->readAll();
	const auto * bytes = reinterpret_cast<const ui8 *>(data.first.get());
	QImage image = readH3Pcx(bytes, data.second);
	if(image.isNull()) // a mod may put a PNG in its place
		image.loadFromData(bytes, static_cast<int>(data.second));
	return QPixmap::fromImage(image);
}

/// A frame of a Heroes III sprite (DEF), decoded as the game's own loader decodes one (client
/// CDefFile): palette index 0 transparent, 1 to 7 the game's shadow, the rest the sprite's palette
QImage readH3DefFrame(const ui8 * def, size_t size, size_t wanted)
{
	const auto fits = [size](size_t at, size_t length) { return at + length <= size; };
	if(!fits(0, 16 + 768))
		return {};
	const ui32 blocks = read_le_u32(def + 12);
	QVector<QRgb> colours;
	for(int i = 0; i < 256; ++i)
		colours.append(i == 0 ? qRgba(0, 0, 0, 0) : i < 8 ? qRgba(0, 0, 0, 128) : qRgb(def[16 + 3 * i], def[17 + 3 * i], def[18 + 3 * i]));

	std::vector<ui32> offsets;
	size_t it = 16 + 768;
	for(ui32 block = 0; block < blocks && fits(it, 16); ++block)
	{
		const ui32 entries = read_le_u32(def + it + 4);
		it += 16 + 13 * static_cast<size_t>(entries);
		for(ui32 entry = 0; entry < entries && fits(it, 4); ++entry, it += 4)
			offsets.push_back(read_le_u32(def + it));
	}
	if(wanted >= offsets.size() || !fits(offsets[wanted], 32))
		return {};

	const ui8 * frame = def + offsets[wanted];
	const size_t base = offsets[wanted] + 32;
	const ui32 format = read_le_u32(frame + 4);
	const int fullWidth = static_cast<int>(read_le_u32(frame + 8));
	const int fullHeight = static_cast<int>(read_le_u32(frame + 12));
	const int width = static_cast<int>(read_le_u32(frame + 16));
	const int height = static_cast<int>(read_le_u32(frame + 20));
	const int left = static_cast<int>(read_le_u32(frame + 24));
	const int top = static_cast<int>(read_le_u32(frame + 28));
	if(fullWidth <= 0 || fullHeight <= 0 || width < 0 || height < 0 || left < 0 || top < 0 || left + width > fullWidth || top + height > fullHeight)
		return {};

	QImage image(fullWidth, fullHeight, QImage::Format_Indexed8);
	image.setColorTable(colours);
	image.fill(0);
	if(format > 3 || (format == 2 && !fits(base, 2)))
		return {};
	size_t following = format == 2 ? base + read_le_u16(def + base) : 0; // format 2's lines follow one another
	for(int y = 0; y < height; ++y)
	{
		uchar * line = image.scanLine(top + y) + left;
		size_t at = following;
		if(format == 0)
		{
			at = base + static_cast<size_t>(y) * width;
			if(!fits(at, width))
				return {};
			std::copy(def + at, def + at + width, line);
			continue;
		}
		if(format == 1)
		{
			if(!fits(base + 4 * static_cast<size_t>(y), 4))
				return {};
			at = base + read_le_u32(def + base + 4 * y);
		}
		else if(format == 3)
		{
			const size_t entry = base + 2 * static_cast<size_t>(y) * (width / 32);
			if(!fits(entry, 2))
				return {};
			at = base + read_le_u16(def + entry);
		}
		// one line of segments: format 1 a code byte then a length, 0xFF for raw pixels; formats 2 and 3
		// one byte, the code in its top three bits (7 for raw) and the length below
		int done = 0;
		while(done < width)
		{
			if(!fits(at, format == 1 ? 2 : 1))
				return {};
			int code, length;
			bool raw;
			if(format == 1)
			{
				code = def[at];
				length = def[at + 1] + 1;
				raw = code == 0xFF;
				at += 2;
			}
			else
			{
				code = def[at] >> 5;
				length = (def[at] & 31) + 1;
				raw = code == 7;
				at += 1;
			}
			length = std::min(length, width - done);
			if(raw)
			{
				if(!fits(at, length))
					return {};
				std::copy(def + at, def + at + length, line + done);
				at += length;
			}
			else
				std::fill(line + done, line + done + length, static_cast<uchar>(code));
			done += length;
		}
		following = at;
	}
	return image.convertToFormat(QImage::Format_ARGB32);
}

/// Heroes III's gold bar, the Random Map Setup's "Show random maps" button (RANSHOW) with its words
/// taken out as DMB's RanShowButton pieces are (AssetGenerator): the 21-pixel scrollwork ends, and one
/// column of the brushed middle (110) repeated, at the launcher's button height
QImage wordlessBar(const QImage & frame)
{
	if(frame.width() < 2 * 21 + 112)
		return {};
	QImage bar(2 * 21 + 64, frame.height(), QImage::Format_ARGB32);
	bar.fill(Qt::transparent);
	QPainter painter(&bar);
	painter.drawImage(0, 0, frame, 0, 0, 21, frame.height());
	for(int x = 21; x < bar.width() - 21; ++x)
		painter.drawImage(x, 0, frame, 110, 0, 1, frame.height());
	painter.drawImage(bar.width() - 21, 0, frame, frame.width() - 21, 0, 21, frame.height());
	painter.end();
	return bar.scaledToHeight(BAR_HEIGHT, Qt::SmoothTransformation);
}

/// Heroes III's blue marbled choice button, the Random Map Setup's (RANWEAK), without its words as
/// DMB's RanButton pieces are: the 6-pixel ends and its two word-free middles, 6-14 and 70-76
QImage wordlessChoice(const QImage & frame)
{
	if(frame.width() < 83)
		return {};
	QImage piece(6 + 9 + 7 + 6, frame.height(), QImage::Format_ARGB32);
	piece.fill(Qt::transparent);
	QPainter painter(&piece);
	painter.drawImage(0, 0, frame, 0, 0, 6, frame.height());
	painter.drawImage(6, 0, frame, 6, 0, 9, frame.height());
	painter.drawImage(15, 0, frame, 70, 0, 7, frame.height());
	painter.drawImage(22, 0, frame, frame.width() - 6, 0, 6, frame.height());
	painter.end();
	return piece;
}

/// The game's gold in silver, for the dark look (K, September 27th: "a dark-silver/grey that's still
/// light enough to contrast the black font"): the grey of each pixel, a little lighter and cooler
QImage silvered(const QImage & image)
{
	QImage result = image.convertToFormat(QImage::Format_ARGB32);
	for(int y = 0; y < result.height(); ++y)
	{
		auto * line = reinterpret_cast<QRgb *>(result.scanLine(y));
		for(int x = 0; x < result.width(); ++x)
		{
			const int grey = qGray(line[x]);
			const auto channel = [grey](int lift) { return std::clamp(grey * 86 / 100 + lift, 0, 255); };
			line[x] = qRgba(channel(0), channel(3), channel(8), qAlpha(line[x]));
		}
	}
	return result;
}

/// a button's frame, through the launcher's own resource access (CResourceHandler, readH3DefFrame);
/// the game engine reads the same buttons a different way (client/render/GameLettering.cpp)
GameButtonLettering::Picture qtButtonPicture(const std::string & name)
{
	const AnimationPath path = AnimationPath::builtin("SPRITES/" + name);
	if(!CResourceHandler::get()->existsResource(path))
		return {};
	const auto data = CResourceHandler::get()->load(path)->readAll();
	const auto * bytes = reinterpret_cast<const ui8 *>(data.first.get());
	const QImage frame = readH3DefFrame(bytes, data.second, 0).convertToFormat(QImage::Format_ARGB32);
	if(frame.isNull())
		return {};
	GameButtonLettering::Picture picture;
	picture.w = frame.width();
	picture.h = frame.height();
	picture.pixels.reserve(static_cast<size_t>(picture.w) * picture.h);
	for(int y = 0; y < picture.h; ++y)
	{
		const auto * line = reinterpret_cast<const QRgb *>(frame.constScanLine(y));
		for(int x = 0; x < picture.w; ++x)
			picture.pixels.emplace_back(qRed(line[x]), qGreen(line[x]), qBlue(line[x]), 255);
	}
	return picture;
}

/// the lettering, lifted from the player's own files the first time a button's word asks for it. The
/// harvest checks the game's own installed language (CGeneralTextHandler::getInstalledLanguage, so a
/// non-English game's buttons, carrying other words, are not read as if they were the game's own
/// English ones); the client always has that answer by the time a game is running (loading a mod's
/// filesystem detects it), the launcher does not unless something has already asked for it, which
/// showing its own buttons does not. Asked for here, once, the same detection the client would trigger.
const GameButtonLettering::Lettering & qtButtonLettering()
{
	static const GameButtonLettering::Lettering lettering = []()
	{
		CGeneralTextHandler::detectInstallParameters();
		return GameButtonLettering::harvest(qtButtonPicture);
	}();
	return lettering;
}

/// `text` in the game's own gold-button lettering (K, overnight into September 28th, after seeing the
/// in-game carved words: "i want to get that exact look in the DMB launcher"): a transparent image, the
/// black or white share of each pixel from the harvest painted on. Null when the lettering could not be
/// read (the language is not English, or the buttons it is read from are missing) or `text` needs a
/// letter the harvest lacks (J, Q, Z, digits, punctuation): the button's own plain text stays, unchanged.
/// A hyphen is the one punctuation mark this drops rather than refuses on ("Input - Mouse" carves as
/// "INPUT MOUSE"; the harvest has no glyph for it and Settings' own category names are the only
/// launcher text that uses one), since losing a hyphen reads fine where losing carving altogether does not.
QImage carvedButtonWord(const QString & text)
{
	const GameButtonLettering::Lettering & lettering = qtButtonLettering();
	if(lettering.band == 0)
		return {};
	QString withoutHyphens = text.trimmed();
	withoutHyphens.replace(QLatin1Char('-'), QLatin1Char(' '));
	const QByteArray upper = withoutHyphens.simplified().toUpper().toLatin1();
	const std::string capitals(upper.constData(), static_cast<size_t>(upper.size()));
	if(capitals.empty())
		return {};
	for(const char c : capitals)
		if(c != ' ' && !lettering.glyphs.count(c))
			return {};

	using Glyph = GameButtonLettering::Glyph;
	constexpr double dark = 0.45; // GameButtonLettering's own DARK: a letter's own pixels, at least this black
	constexpr int letterGap = 1;  // the game's own spacing (GameLettering.cpp): a pixel between letters
	constexpr int wordGap = 6;    // eight between words, so six added to the letter gap already crossed
	constexpr int closest = 1;
	constexpr int farthest = 5;
	struct Placed
	{
		const Glyph * glyph;
		int x;
	};
	std::vector<Placed> placed;
	std::map<int, int> previous; // the letter before's last black column in each row, at its place
	int x = 0;
	for(const char c : capitals)
	{
		if(c == ' ')
		{
			x += wordGap;
			previous.clear();
			continue;
		}
		const Glyph & glyph = lettering.glyphs.at(c);
		std::map<int, std::pair<int, int>> rows;
		for(const auto & [cell, share] : glyph.pixels)
			if(share.black >= dark)
			{
				const int row = cell.second;
				const auto it = rows.find(row);
				if(it == rows.end())
					rows[row] = {cell.first, cell.first};
				else
					it->second = {std::min(it->second.first, cell.first), std::max(it->second.second, cell.first)};
			}
		int at = x;
		std::optional<int> nearest;
		for(const auto & [row, span] : rows)
			if(previous.count(row))
			{
				const int gap = at + span.first - previous.at(row) - 1;
				nearest = nearest ? std::min(*nearest, gap) : gap;
			}
		if(nearest && *nearest < closest)
			at += closest - *nearest;
		else if(nearest && *nearest > farthest)
			at -= std::min(*nearest - farthest, 2);
		placed.push_back({&glyph, at});
		previous.clear();
		for(const auto & [row, span] : rows)
			previous[row] = at + span.second;
		x = at + glyph.width + letterGap;
	}
	const int width = x - letterGap;
	if(width <= 0)
		return {};

	QImage image(width, lettering.band, QImage::Format_ARGB32);
	image.fill(qRgba(0, 0, 0, 0));
	for(const auto & p : placed)
		for(const auto & [cell, share] : p.glyph->pixels)
		{
			const double a = share.black + share.white;
			if(a <= 0)
				continue;
			const int px = p.x + cell.first;
			const int py = cell.second;
			if(px < 0 || px >= width || py < 0 || py >= lettering.band)
				continue;
			const auto value = static_cast<int>(std::lround(std::clamp(255.0 * share.white / a, 0.0, 255.0)));
			const auto opacity = static_cast<int>(std::lround(std::clamp(a, 0.0, 1.0) * 255));
			image.setPixelColor(px, py, QColor(value, value, value, opacity));
		}
	// the letters, at the height the buttons they were read from are (40, RANSHOW's own, confirmed
	// against every button the harvest reads from), scaled the same amount the bar itself is
	// (BAR_HEIGHT / 40, wordlessBar), so they stand in its own proportion, not oversized on a shrunk bar
	const int scaledHeight = std::max(1, static_cast<int>(std::lround(image.height() * BAR_HEIGHT / 40.0)));
	return image.scaledToHeight(scaledHeight, Qt::SmoothTransformation);
}

// the bar's own scrollwork ends, kept clear on each side (gameButtonStyle's own 21 px at RANSHOW's
// native size, scaled the same amount the carved word already is), and a little headroom the
// button's own frame needs top and bottom
int carvedBarEnds()
{
	return std::max(1, static_cast<int>(std::lround(21 * BAR_HEIGHT / 40.0)));
}
constexpr int carvedClear = 2;

/// `button` in the exact carved-gold lettering (K, overnight into September 28th: "i want to get that
/// exact look in the DMB launcher") where `active` (the look is leather or dark, not "system") and its
/// own word fits and every letter of it is there; back to its own plain text otherwise, whether that is
/// because the look is "system" now, the button's own size shrank past fitting, or its word needs a
/// letter the harvest lacks (J, Q, Z, digits, punctuation: "Install HD Edition (Steam)", any "?" help
/// button). Idempotent: safe to call again for a button already carved, or already plain.
void applyCarvedButtonWord(QPushButton * button, bool active)
{
	const QVariant stored = button->property("dmbCarvedFrom");
	const QString original = stored.isValid() ? stored.toString() : button->text();
	if(!stored.isValid())
		button->setProperty("dmbCarvedFrom", original);
	const QImage carved = active ? carvedButtonWord(original) : QImage();
	const bool fits = !carved.isNull() && carved.width() + 2 * (carvedBarEnds() + carvedClear) <= button->width()
		&& carved.height() + carvedClear <= button->height();
	if(fits)
	{
		button->setIcon(QIcon(QPixmap::fromImage(carved)));
		button->setIconSize(carved.size());
		button->setText(QString());
	}
	else if(button->text().isEmpty() && !button->icon().isNull())
	{
		button->setIcon(QIcon());
		button->setText(original);
	}
}

void carveEveryButtonUnder(QWidget * window, bool active)
{
	for(QPushButton * button : window->findChildren<QPushButton *>())
		applyCarvedButtonWord(button, active);
}

/// `applyCarvedButtonWord` on every button of every open window (a theme switch: a widget's own layout,
/// and so its width, is already real by then, every window having shown at least once already)
void carveEveryButton(bool active)
{
	for(QWidget * top : QApplication::topLevelWidgets())
		carveEveryButtonUnder(top, active);
}

/// The launcher's buttons as the game's own gold bars (K, September 26th: "these buttons need to
/// actually mimic the heroes 3 buttons exactly"), from the player's own game files, written to the
/// user's cache for the style sheet to name, in silver for the dark look unless the player keeps gold.
/// The Settings page's categories are the same bars (K, September 27th). Empty before an import: then
/// they keep the painted gold.
QString gameButtonStyle(bool silver)
{
	const AnimationPath path = AnimationPath::builtin("SPRITES/RANSHOW");
	if(!CResourceHandler::get()->existsResource(path))
		return {};
	const auto data = CResourceHandler::get()->load(path)->readAll();
	const auto * bytes = reinterpret_cast<const ui8 *>(data.first.get());
	QImage normal = wordlessBar(readH3DefFrame(bytes, data.second, 0));
	QImage pressed = wordlessBar(readH3DefFrame(bytes, data.second, 1));
	QImage lit = wordlessBar(readH3DefFrame(bytes, data.second, 3));
	if(normal.isNull() || pressed.isNull() || lit.isNull())
		return {};
	if(silver)
	{
		normal = silvered(normal);
		pressed = silvered(pressed);
		lit = silvered(lit);
	}
	// a button that cannot be pressed: the normal bar, dimmed
	QImage dimmed = normal;
	for(int y = 0; y < dimmed.height(); ++y)
	{
		auto * line = reinterpret_cast<QRgb *>(dimmed.scanLine(y));
		for(int x = 0; x < dimmed.width(); ++x)
			line[x] = qRgba(qRed(line[x]) * 55 / 100, qGreen(line[x]) * 50 / 100, qBlue(line[x]) * 45 / 100, qAlpha(line[x]));
	}

	const auto folder = VCMIDirs::get().userCachePath() / "launcher";
	boost::system::error_code ec;
	boost::filesystem::create_directories(folder, ec);
	const auto write = [&folder](const QImage & image, const std::string & name)
	{
		const QString file = QString::fromStdString((folder / (name + ".png")).string());
		return image.save(file, "PNG") ? QDir::fromNativeSeparators(file) : QString();
	};
	const std::string metal = silver ? "-silver" : "";
	const QString files[] = { write(normal, "dmb-button" + metal), write(pressed, "dmb-button-pressed" + metal),
		write(lit, "dmb-button-lit" + metal), write(dimmed, "dmb-button-disabled" + metal) };
	for(const auto & file : files)
		if(file.isEmpty())
			return {};

	// the scrollwork ends keep their size, the middle stretches to the button
	const int end = normal.width() * 21 / (2 * 21 + 64);
	const auto face = [end](const QString & file)
	{
		return QString("border-image: url(\"%1\") 4 %2 4 %2 stretch stretch;").arg(file).arg(end);
	};
	QString style = QString(
		"QPushButton { color: black; background: transparent; border-radius: 0; border-width: 4px %1px; padding: 1px 2px; %2 }"
		"QPushButton:hover { %3 }"
		"QPushButton:pressed { %4 }"
		// Gemini's read of K's launcher screenshot, relayed by K overnight into September 28th: "Import
		// from Clipboard" grayed out was dark text on the dimmed bar's own dark gold, hard to read. A
		// light parchment tone (the same one the dark look already uses in gold's place) reads clearly
		// against the dimmed bar in either look, where the near-black original barely stood out from it.
		"QPushButton:disabled { color: #f3e7c9; %5 }"
		// the Settings page's categories: the same bars, the open one pressed in
		"#dmbSettingsCategories QPushButton { color: black; background: transparent; border-radius: 0; border-width: 4px %1px;"
		" padding: 3px 6px; text-align: center; font-weight: bold; %2 }"
		"#dmbSettingsCategories QPushButton:hover { color: black; background: transparent; %3 }"
		"#dmbSettingsCategories QPushButton:checked { color: black; background: transparent; %4 }")
		.arg(end).arg(face(files[0]), face(files[2]), face(files[1]), face(files[3]));

	// the Settings page's switches as the Random Map Setup's choices: blue, and framed in gold when on;
	// their marbled middle repeats rather than stretching
	const AnimationPath choicePath = AnimationPath::builtin("SPRITES/RANWEAK");
	if(!CResourceHandler::get()->existsResource(choicePath))
		return style;
	const auto choiceData = CResourceHandler::get()->load(choicePath)->readAll();
	const auto * choiceBytes = reinterpret_cast<const ui8 *>(choiceData.first.get());
	const QString off = write(wordlessChoice(readH3DefFrame(choiceBytes, choiceData.second, 0)), "dmb-choice");
	const QString on = write(wordlessChoice(readH3DefFrame(choiceBytes, choiceData.second, 3)), "dmb-choice-on");
	if(off.isEmpty() || on.isEmpty())
		return style;
	return style + QString(
		"CSettingsView QToolButton { color: #e8c86a; background: transparent; border-radius: 0; border-width: 4px 6px; padding: 1px 4px;"
		" border-image: url(\"%1\") 4 6 4 6 repeat stretch; }"
		"CSettingsView QToolButton:checked { color: #fff0b0; background: transparent; border-image: url(\"%2\") 4 6 4 6 repeat stretch; }"
		"CSettingsView QToolButton:hover { color: #fbe29a; }"
		"CSettingsView QToolButton:disabled { color: #7d6a4c; }")
		.arg(off, on);
}

/// Dark mode's background: the same leather tinted toward black, so the two looks share one grain
/// (K, September 26th: one harvested texture in two tints, not a second dark design)
QPixmap darkened(const QPixmap & leather)
{
	QImage image = leather.toImage().convertToFormat(QImage::Format_RGB32);
	for(int y = 0; y < image.height(); ++y)
	{
		auto * line = reinterpret_cast<QRgb *>(image.scanLine(y));
		for(int x = 0; x < image.width(); ++x)
		{
			// two thirds of the way to grey, then down to 30% of the brightness: the grain stays, the brown mostly goes
			const int grey = qGray(line[x]);
			const auto tint = [grey](int channel) { return (channel + 2 * grey) / 3 * 30 / 100; };
			line[x] = qRgb(tint(qRed(line[x])), tint(qGreen(line[x])), tint(qBlue(line[x])));
		}
	}
	return QPixmap::fromImage(image);
}

const QColor gold(217, 168, 62);
const QColor parchment(243, 231, 201);

// Gold buttons with black text, as the game draws its own; the side menu and tabs in the same gold. The
// Settings page's categories gold on black down its left, the category's settings framed in gold, its
// dropdowns and its buttons that switch a setting in the game's gold (K, September 26th).
const char * commonStyle = R"(
QPushButton {
	color: black;
	background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #f3d27a, stop:0.5 #d9a83e, stop:1 #9c6d1c);
	border: 1px solid #4a3008;
	border-radius: 3px;
	padding: 4px 12px;
}
QPushButton:hover { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #fbe29a, stop:0.5 #e8bc55, stop:1 #ae7e2a); }
QPushButton:pressed { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #9c6d1c, stop:1 #d9a83e); }
QPushButton:disabled { color: #3a2a12; background: #7d6a4c; border-color: #3a2a12; }
QToolButton { background: transparent; border: 1px solid transparent; border-radius: 4px; padding: 2px; }
QToolButton:checked, QToolButton:hover { background: rgba(0, 0, 0, 90); border: 1px solid #d9a83e; }
QTabBar::tab { padding: 4px 14px; border: 1px solid #8a6a2a; border-bottom: none; }
QTabBar::tab:selected { color: black; background: #d9a83e; }
QHeaderView::section { padding: 3px 6px; border: none; border-right: 1px solid #8a6a2a; border-bottom: 1px solid #8a6a2a; }
QToolTip { color: #f3e7c9; background: #2a1a0a; border: 1px solid #d9a83e; }
QProgressBar { border: 1px solid #8a6a2a; text-align: center; }
QProgressBar::chunk { background: #d9a83e; }
#dmbSettingsCategories QPushButton {
	color: #d9a83e;
	background: black;
	border-image: none;
	border: 1px solid #4a3008;
	border-radius: 0;
	padding: 6px 14px;
	text-align: left;
	font-weight: bold;
}
#dmbSettingsCategories QPushButton:hover { color: #fbe29a; background: #1c1307; }
#dmbSettingsCategories QPushButton:checked { color: #fbe29a; background: #2b1d08; border: 1px solid #d9a83e; }
#dmbSettingsPanel { border: 2px solid #d9a83e; border-radius: 3px; }
QLabel[dmbSettingsTitle="true"] { color: #d9a83e; font-size: 13pt; font-weight: bold; }
CSettingsView QComboBox { color: #f3e7c9; background: #24170a; border: 1px solid #d9a83e; border-radius: 3px; padding: 3px 8px; }
CSettingsView QComboBox:hover { border-color: #fbe29a; }
CSettingsView QComboBox QAbstractItemView { color: #f3e7c9; background: #1a1108; border: 1px solid #d9a83e; selection-color: black; selection-background-color: #d9a83e; }
CSettingsView QToolButton { color: #f3e7c9; background: #24170a; border: 1px solid #8a6a2a; border-radius: 3px; padding: 3px 10px; }
CSettingsView QToolButton:hover { border-color: #d9a83e; }
CSettingsView QToolButton:checked {
	color: black;
	background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #f3d27a, stop:0.5 #d9a83e, stop:1 #9c6d1c);
	border: 1px solid #4a3008;
}
)";

QPalette themedPalette(const QBrush & window, const QColor & base, const QColor & alternate, const QColor & button,
	const QColor & accent = gold, const QColor & text = parchment)
{
	QPalette palette;
	palette.setBrush(QPalette::Window, window);
	palette.setColor(QPalette::WindowText, text);
	palette.setColor(QPalette::Base, base);
	palette.setColor(QPalette::AlternateBase, alternate);
	palette.setColor(QPalette::Text, text);
	palette.setColor(QPalette::PlaceholderText, QColor(170, 150, 110));
	palette.setColor(QPalette::Button, button);
	palette.setColor(QPalette::ButtonText, text);
	palette.setColor(QPalette::BrightText, accent);
	palette.setColor(QPalette::Highlight, accent);
	palette.setColor(QPalette::HighlightedText, Qt::black);
	palette.setColor(QPalette::Link, accent);
	palette.setColor(QPalette::ToolTipBase, QColor(42, 26, 10));
	palette.setColor(QPalette::ToolTipText, text);
	palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(150, 135, 105));
	palette.setColor(QPalette::Disabled, QPalette::Text, QColor(150, 135, 105));
	palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(150, 135, 105));
	return palette;
}

/// The style sheet's gold, its browns and its parchment text in the silver and greys of the dark look
QString inSilver(QString style)
{
	static const std::vector<std::pair<const char *, const char *>> colours = {
		{"#f3d27a", "#c6cad1"}, {"#d9a83e", "#9fa4ac"}, {"#9c6d1c", "#686c73"}, {"#fbe29a", "#d9dce1"},
		{"#e8bc55", "#b2b6bd"}, {"#ae7e2a", "#7b7f86"}, {"#8a6a2a", "#5c6067"}, {"#4a3008", "#2f3237"},
		{"#3a2a12", "#2a2c30"}, {"#7d6a4c", "#686b70"}, {"#1c1307", "#1a1a1b"}, {"#2b1d08", "#2a2b2d"},
		{"#24170a", "#202122"}, {"#1a1108", "#171718"}, {"#2a1a0a", "#1e1e1f"}, {"#e8c86a", "#d7dbe1"},
		{"#fff0b0", "#ffffff"}, {"#2a1c0c", "#222326"}, {"#f3e7c9", "#e8eaed"},
	};
	for(const auto & [goldColour, silverColour] : colours)
		style.replace(goldColour, silverColour, Qt::CaseInsensitive);
	return style;
}
}

// the kinds of widget Qt gives a platform font of their own (QApplication's widget font table)
const char * const WIDGETS_WITH_OWN_FONTS[] = { "QAbstractItemView", "QListView", "QHeaderView", "QMenu", "QMenuItem", "QMenuBar",
	"QTipLabel", "QMessageBox", "QStatusBar", "QComboMenuItem", "QComboLineEdit", "QLabel", "QPushButton", "QCheckBox",
	"QRadioButton", "QToolButton", "QTabBar", "QMdiSubWindowTitleBar", "QDockWidgetTitle", "QSmallFont", "QMiniFont" };

/// The game's own fonts for the leather and dark looks (gamefont.h): its MEDFONT for text and BIGFONT
/// for titles, each at the one size that draws the game's pixels exactly, without smoothing, as the
/// game draws them. Empty before the game's files are imported: the platform's font stays.
static QString gameFontStyle()
{
	const GameFont::Font text = GameFont::load("MEDFONT", "DMB Heroes");
	const GameFont::Font title = GameFont::load("BIGFONT", "DMB Heroes Big");
	if(text.family.isEmpty())
		return {};
	QFont font(text.family);
	font.setPixelSize(text.height);
	font.setStyleStrategy(QFont::NoAntialias);
	QApplication::setFont(font);
	// the platform gives some kinds of widget fonts of their own (a list's column headers, menus,
	// tooltips, message boxes), which the application's font does not reach
	for(const char * kind : WIDGETS_WITH_OWN_FONTS)
		QApplication::setFont(font, kind);
	// the style sheet's bold and point sizes would smear the game's pixels: its own weight and size
	QString style = QString(
		"#dmbSettingsCategories QPushButton { font-weight: normal; }"
		"QLabel[dmbSettingsTitle=\"true\"] { font-weight: normal; font-size: %1px; }"
		// lists and their column headers keep a platform font of their own otherwise
		"QAbstractItemView, QHeaderView::section { font-family: \"%2\"; font-size: %1px; }").arg(text.height).arg(text.family);
	if(!title.family.isEmpty())
		style += QString("QLabel[dmbSettingsTitle=\"true\"] { font-family: \"%1\"; font-size: %2px; }").arg(title.family).arg(title.height);
	return style;
}

int LauncherTheme::minimumCarvedButtonWidth(const QString & text)
{
	const QImage carved = carvedButtonWord(text);
	if(carved.isNull())
		return 0;
	return carved.width() + 2 * (carvedBarEnds() + carvedClear);
}

void LauncherTheme::apply()
{
	// the platform's own look, kept from the first call, for switching back to "system" from the Settings page
	static const QString systemStyle = QApplication::style()->objectName(); // Qt 5 keeps the style's key there
	static const QPalette systemPalette = QApplication::palette();
	static const QFont systemFont = QApplication::font();
	static const std::map<std::string, QFont> systemFonts = []()
	{
		std::map<std::string, QFont> fonts;
		for(const char * kind : WIDGETS_WITH_OWN_FONTS)
			fonts[kind] = QApplication::font(kind);
		return fonts;
	}();
	static FrameEveryWindow * const framer = []()
	{
		auto * filter = new FrameEveryWindow();
		qApp->installEventFilter(filter);
		return filter;
	}();
	(void)framer;

	const std::string theme = settings["launcher"]["theme"].String();
	// the title bars of windows already open follow at once; later ones as they show
	darkFrames = theme != "system";
	for(QWidget * window : QApplication::topLevelWidgets())
		if(window->isVisible())
			frameWindow(window);

	if(theme == "system")
	{
		QApplication::setStyle(QStyleFactory::create(systemStyle));
		QApplication::setPalette(systemPalette);
		QApplication::setFont(systemFont);
		for(const auto & [kind, font] : systemFonts)
			QApplication::setFont(font, kind.c_str());
		qApp->setStyleSheet(QString());
		carveEveryButton(false);
		logGlobal->info("Launcher look: system");
		return;
	}

	const QPixmap leather = playerLeather();

	QApplication::setStyle(QStyleFactory::create("Fusion")); // the one style that follows the palette everywhere
	if(theme == "leather" && !leather.isNull())
	{
		// the tile itself behind every window; lists and text on a dark brown, so they stay readable
		QApplication::setPalette(themedPalette(QBrush(leather), QColor(34, 22, 10), QColor(46, 30, 14), QColor(70, 46, 20)));
		qApp->setStyleSheet(QString(commonStyle) + gameButtonStyle(false) +
			"QTabBar::tab, QHeaderView::section { color: #f3e7c9; background: #3a2410; }" + gameFontStyle());
		logGlobal->info("Launcher look: leather, from the player's game files");
	}
	else
	{
		// the same leather tinted near black; a flat near black only before the game files are imported.
		// Its buttons and edges in silver, unless the player keeps the gold (launcher.darkAccent)
		const bool silver = settings["launcher"]["darkAccent"].String() != "gold";
		const QBrush window = leather.isNull() ? QBrush(QColor(24, 24, 24)) : QBrush(darkened(leather));
		const QString style = QString(commonStyle) + gameButtonStyle(silver) +
			"QTabBar::tab, QHeaderView::section { color: #f3e7c9; background: #2c2c2c; }" + gameFontStyle();
		QApplication::setPalette(themedPalette(window, QColor(16, 16, 16), QColor(30, 30, 30), QColor(44, 44, 44),
			silver ? QColor(159, 164, 172) : gold, silver ? QColor(232, 234, 237) : parchment));
		qApp->setStyleSheet(silver ? inSilver(style) : style);
		logGlobal->info("Launcher look: dark in %s, %s", silver ? "silver" : "gold", leather.isNull()
			? "flat (no game files to read the leather from yet)" : "the leather tinted near black");
	}
	carveEveryButton(true);
}
