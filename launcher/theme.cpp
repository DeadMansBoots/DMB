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

#include "../lib/CConfigHandler.h"
#include "../lib/filesystem/Filesystem.h"
#include "../lib/vcmi_endian.h"

#include <QApplication>
#include <QPalette>
#include <QPixmap>
#include <QStyleFactory>

namespace
{
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

// Gold buttons with black text, as the game draws its own; the side menu and tabs in the same gold
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
)";

QPalette themedPalette(const QBrush & window, const QColor & base, const QColor & alternate, const QColor & button)
{
	QPalette palette;
	palette.setBrush(QPalette::Window, window);
	palette.setColor(QPalette::WindowText, parchment);
	palette.setColor(QPalette::Base, base);
	palette.setColor(QPalette::AlternateBase, alternate);
	palette.setColor(QPalette::Text, parchment);
	palette.setColor(QPalette::PlaceholderText, QColor(170, 150, 110));
	palette.setColor(QPalette::Button, button);
	palette.setColor(QPalette::ButtonText, parchment);
	palette.setColor(QPalette::BrightText, gold);
	palette.setColor(QPalette::Highlight, gold);
	palette.setColor(QPalette::HighlightedText, Qt::black);
	palette.setColor(QPalette::Link, gold);
	palette.setColor(QPalette::ToolTipBase, QColor(42, 26, 10));
	palette.setColor(QPalette::ToolTipText, parchment);
	palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(150, 135, 105));
	palette.setColor(QPalette::Disabled, QPalette::Text, QColor(150, 135, 105));
	palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(150, 135, 105));
	return palette;
}
}

void LauncherTheme::apply()
{
	const std::string theme = settings["launcher"]["theme"].String();
	if(theme == "system")
		return;

	const QPixmap leather = playerLeather();

	QApplication::setStyle(QStyleFactory::create("Fusion")); // the one style that follows the palette everywhere
	if(theme == "leather" && !leather.isNull())
	{
		// the tile itself behind every window; lists and text on a dark brown, so they stay readable
		QApplication::setPalette(themedPalette(QBrush(leather), QColor(34, 22, 10), QColor(46, 30, 14), QColor(70, 46, 20)));
		qApp->setStyleSheet(QString(commonStyle) +
			"QTabBar::tab, QHeaderView::section { color: #f3e7c9; background: #3a2410; }");
		logGlobal->info("Launcher look: leather, from the player's game files");
	}
	else
	{
		// the same leather tinted near black; a flat near black only before the game files are imported
		const QBrush window = leather.isNull() ? QBrush(QColor(24, 24, 24)) : QBrush(darkened(leather));
		QApplication::setPalette(themedPalette(window, QColor(16, 16, 16), QColor(30, 30, 30), QColor(44, 44, 44)));
		qApp->setStyleSheet(QString(commonStyle) +
			"QTabBar::tab, QHeaderView::section { color: #f3e7c9; background: #2c2c2c; }");
		logGlobal->info("Launcher look: dark, %s", leather.isNull() ? "flat (no game files to read the leather from yet)"
			: "the leather tinted near black");
	}
}
