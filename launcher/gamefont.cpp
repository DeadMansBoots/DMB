/*
 * gamefont.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "gamefont.h"

#include <QFontDatabase>

#include "../lib/CConfigHandler.h"
#include "../lib/filesystem/Filesystem.h"
#include "../lib/texts/Languages.h"
#include "../lib/texts/TextOperations.h"

namespace
{
/// font units to a pixel of the game's glyphs
constexpr int UNIT = 64;

/// TrueType's big-endian fields
class Writer
{
public:
	QByteArray data;

	void u8(int value) { data.append(static_cast<char>(value & 0xFF)); }
	void u16(int value) { u8(value >> 8); u8(value); }
	void i16(int value) { u16(static_cast<uint16_t>(static_cast<int16_t>(value))); }
	void u32(uint32_t value) { u16(static_cast<int>(value >> 16)); u16(static_cast<int>(value & 0xFFFF)); }
	void tag(const char * name) { data.append(name, 4); }
	void pad() { while(data.size() % 4) u8(0); }
};

uint32_t checksum(const QByteArray & bytes)
{
	uint32_t sum = 0;
	for(int i = 0; i < bytes.size(); i += 4)
	{
		uint32_t word = 0;
		for(int j = 0; j < 4; ++j)
			word = (word << 8) | (i + j < bytes.size() ? static_cast<uint8_t>(bytes[i + j]) : 0u);
		sum += word;
	}
	return sum;
}

/// One of the game font's 256 glyphs as boxes in font units: each run of the glyph's pixels in a row,
/// runs in the same columns on the rows below joined into one box
struct Glyph
{
	int advance = 0;
	std::vector<std::array<int, 4>> boxes; // left, bottom, right, top
	int xMin = 0, yMin = 0, xMax = 0, yMax = 0;
};

QByteArray utf16be(const QString & text)
{
	QByteArray bytes;
	for(const QChar c : text)
	{
		bytes.append(static_cast<char>(c.unicode() >> 8));
		bytes.append(static_cast<char>(c.unicode() & 0xFF));
	}
	return bytes;
}
}

QByteArray GameFont::trueType(const QByteArray & fnt, const QString & family, const std::string & encoding, int & height)
{
	// The game's font files: the height in byte 5; at 32, each glyph's widths before, of and after its
	// pixels (A, B, C, 32-bit each); then each glyph's pixel offset; then the pixels, one byte each,
	// 0 empty, 1 the black shadow, anything else the text (client/renderSDL/CBitmapFont.cpp)
	constexpr int glyphCount = 256;
	constexpr int widthsAt = 32;
	constexpr int offsetsAt = widthsAt + glyphCount * 12;
	constexpr int pixelsAt = offsetsAt + glyphCount * 4;
	if(fnt.size() < pixelsAt)
		return {};
	const auto * bytes = reinterpret_cast<const uint8_t *>(fnt.constData());
	const auto le32 = [bytes](int at) { return static_cast<int32_t>(bytes[at] | bytes[at + 1] << 8 | bytes[at + 2] << 16 | static_cast<uint32_t>(bytes[at + 3]) << 24); };
	height = bytes[5];
	if(height <= 0)
		return {};

	struct Source
	{
		int left, width, right;
		const uint8_t * pixels;
	};
	std::vector<Source> sources;
	for(int c = 0; c < glyphCount; ++c)
	{
		Source source { le32(widthsAt + c * 12), le32(widthsAt + c * 12 + 4), le32(widthsAt + c * 12 + 8), nullptr };
		const int64_t at = static_cast<int64_t>(pixelsAt) + static_cast<uint32_t>(le32(offsetsAt + c * 4));
		if(source.width < 0 || source.width > 1024 || at + static_cast<int64_t>(source.width) * height > fnt.size())
			return {};
		source.pixels = bytes + at;
		sources.push_back(source);
	}

	// the baseline as VCMI finds it: under the lowest row of "L"
	int ascent = height;
	{
		const Source & l = sources['L'];
		int lowest = -1;
		for(int row = 0; row < height; ++row)
			for(int col = 0; col < l.width; ++col)
				if(l.pixels[row * l.width + col] >= 2)
					lowest = row;
		if(lowest >= 0)
			ascent = lowest + 1;
	}
	const int descent = height - ascent;

	// glyph 0 is the empty .notdef; glyph c + 1 is the game's glyph c
	std::vector<Glyph> glyphs(glyphCount + 1);
	glyphs[0].advance = height * UNIT / 2;
	for(int c = 0; c < glyphCount; ++c)
	{
		const Source & source = sources[c];
		Glyph & glyph = glyphs[c + 1];
		glyph.advance = std::max(0, source.left + source.width + source.right) * UNIT;
		std::map<std::pair<int, int>, size_t> open;
		for(int row = 0; row < height; ++row)
		{
			std::map<std::pair<int, int>, size_t> next;
			const int top = (ascent - row) * UNIT;
			const int bottom = top - UNIT;
			for(int col = 0; col < source.width;)
			{
				if(source.pixels[row * source.width + col] < 2)
				{
					++col;
					continue;
				}
				const int start = col;
				while(col < source.width && source.pixels[row * source.width + col] >= 2)
					++col;
				const auto run = std::make_pair(start, col);
				if(const auto found = open.find(run); found != open.end())
				{
					glyph.boxes[found->second][1] = bottom;
					next[run] = found->second;
				}
				else
				{
					glyph.boxes.push_back({ (source.left + start) * UNIT, bottom, (source.left + col) * UNIT, top });
					next[run] = glyph.boxes.size() - 1;
				}
			}
			open = std::move(next);
		}
		if(!glyph.boxes.empty())
		{
			glyph.xMin = glyph.yMin = std::numeric_limits<int>::max();
			glyph.xMax = glyph.yMax = std::numeric_limits<int>::min();
			for(const auto & box : glyph.boxes)
			{
				glyph.xMin = std::min(glyph.xMin, box[0]);
				glyph.yMin = std::min(glyph.yMin, box[1]);
				glyph.xMax = std::max(glyph.xMax, box[2]);
				glyph.yMax = std::max(glyph.yMax, box[3]);
			}
		}
	}

	// the characters, in the font's encoding (the game's data language, as VCMI reads it)
	std::map<uint32_t, int> characters; // code point, glyph
	for(int c = 32; c < glyphCount; ++c)
	{
		const uint32_t codepoint = TextOperations::getUnicodeCodepoint(static_cast<char>(c), encoding);
		if(codepoint >= 32 && codepoint < 0xFFFF && !characters.count(codepoint))
			characters[codepoint] = c + 1;
	}
	if(characters.empty())
		return {};

	const int unitsPerEm = height * UNIT;
	int xMin = 0, yMin = 0, xMax = 0, yMax = 0, maxPoints = 0, maxContours = 0, advanceMax = 0;
	int minLeft = std::numeric_limits<int>::max(), minRight = std::numeric_limits<int>::max(), advanceSum = 0, advanceCount = 0;
	for(const auto & glyph : glyphs)
	{
		advanceMax = std::max(advanceMax, glyph.advance);
		if(glyph.advance > 0)
		{
			advanceSum += glyph.advance;
			++advanceCount;
		}
		if(glyph.boxes.empty())
			continue;
		xMin = std::min(xMin, glyph.xMin);
		yMin = std::min(yMin, glyph.yMin);
		xMax = std::max(xMax, glyph.xMax);
		yMax = std::max(yMax, glyph.yMax);
		minLeft = std::min(minLeft, glyph.xMin);
		minRight = std::min(minRight, glyph.advance - glyph.xMax);
		maxPoints = std::max(maxPoints, static_cast<int>(glyph.boxes.size()) * 4);
		maxContours = std::max(maxContours, static_cast<int>(glyph.boxes.size()));
	}
	if(minLeft == std::numeric_limits<int>::max())
		minLeft = minRight = 0;

	std::map<std::string, QByteArray> tables;

	{
		// the outlines: each box four corners, clockwise, every point on the curve and every coordinate a
		// 16-bit step from the one before
		Writer glyf;
		Writer loca;
		for(const auto & glyph : glyphs)
		{
			loca.u32(static_cast<uint32_t>(glyf.data.size()));
			if(glyph.boxes.empty())
				continue;
			glyf.i16(static_cast<int>(glyph.boxes.size()));
			glyf.i16(glyph.xMin);
			glyf.i16(glyph.yMin);
			glyf.i16(glyph.xMax);
			glyf.i16(glyph.yMax);
			for(size_t i = 0; i < glyph.boxes.size(); ++i)
				glyf.u16(static_cast<int>(i * 4 + 3));
			glyf.u16(0); // no instructions
			for(size_t i = 0; i < glyph.boxes.size() * 4; ++i)
				glyf.u8(0x01);
			std::vector<std::pair<int, int>> points;
			for(const auto & box : glyph.boxes)
			{
				points.emplace_back(box[0], box[1]);
				points.emplace_back(box[0], box[3]);
				points.emplace_back(box[2], box[3]);
				points.emplace_back(box[2], box[1]);
			}
			int last = 0;
			for(const auto & point : points)
			{
				glyf.i16(point.first - last);
				last = point.first;
			}
			last = 0;
			for(const auto & point : points)
			{
				glyf.i16(point.second - last);
				last = point.second;
			}
			glyf.pad();
		}
		loca.u32(static_cast<uint32_t>(glyf.data.size()));
		tables["glyf"] = glyf.data;
		tables["loca"] = loca.data;
	}

	{
		Writer hmtx;
		for(const auto & glyph : glyphs)
		{
			hmtx.u16(glyph.advance);
			hmtx.i16(glyph.boxes.empty() ? 0 : glyph.xMin);
		}
		tables["hmtx"] = hmtx.data;
	}

	{
		// Windows' own character map (platform 3, Unicode BMP): runs of characters whose glyphs follow
		// one another, then the closing segment
		std::vector<std::array<int, 3>> segments; // first, last, delta
		for(const auto & [codepoint, glyph] : characters)
		{
			const int delta = glyph - static_cast<int>(codepoint);
			if(!segments.empty() && segments.back()[1] + 1 == static_cast<int>(codepoint) && segments.back()[2] == delta)
				segments.back()[1] = static_cast<int>(codepoint);
			else
				segments.push_back({ static_cast<int>(codepoint), static_cast<int>(codepoint), delta });
		}
		segments.push_back({ 0xFFFF, 0xFFFF, 1 });
		const int segCount = static_cast<int>(segments.size());
		int power = 1, log2 = 0;
		while(power * 2 <= segCount)
		{
			power *= 2;
			++log2;
		}
		Writer map;
		map.u16(4);
		map.u16(16 + segCount * 8);
		map.u16(0);
		map.u16(segCount * 2);
		map.u16(power * 2);
		map.u16(log2);
		map.u16(segCount * 2 - power * 2);
		for(const auto & segment : segments)
			map.u16(segment[1]);
		map.u16(0);
		for(const auto & segment : segments)
			map.u16(segment[0]);
		for(const auto & segment : segments)
			map.u16(segment[2] & 0xFFFF);
		for(size_t i = 0; i < segments.size(); ++i)
			map.u16(0);
		Writer cmap;
		cmap.u16(0);
		cmap.u16(1);
		cmap.u16(3);
		cmap.u16(1);
		cmap.u32(12);
		cmap.data.append(map.data);
		tables["cmap"] = cmap.data;
	}

	{
		Writer head;
		head.u32(0x00010000);
		head.u32(0x00010000);
		head.u32(0); // checkSumAdjustment, below
		head.u32(0x5F0F3CF5);
		head.u16(0x000B); // baseline at 0, left side bearing at 0, whole-pixel sizes
		head.u16(unitsPerEm);
		for(int i = 0; i < 4; ++i)
			head.u32(0); // created and modified
		head.i16(xMin);
		head.i16(yMin);
		head.i16(xMax);
		head.i16(yMax);
		head.u16(0);
		head.u16(height);
		head.i16(2);
		head.i16(1); // 32-bit glyph offsets
		head.i16(0);
		tables["head"] = head.data;
	}

	{
		Writer hhea;
		hhea.u32(0x00010000);
		hhea.i16(ascent * UNIT);
		hhea.i16(-descent * UNIT);
		hhea.i16(0);
		hhea.u16(advanceMax);
		hhea.i16(minLeft);
		hhea.i16(minRight);
		hhea.i16(xMax);
		hhea.i16(1);
		hhea.i16(0);
		hhea.i16(0);
		for(int i = 0; i < 4; ++i)
			hhea.i16(0);
		hhea.i16(0);
		hhea.u16(static_cast<int>(glyphs.size()));
		tables["hhea"] = hhea.data;
	}

	{
		Writer maxp;
		maxp.u32(0x00010000);
		maxp.u16(static_cast<int>(glyphs.size()));
		maxp.u16(maxPoints);
		maxp.u16(maxContours);
		maxp.u16(0);
		maxp.u16(0);
		maxp.u16(2);
		for(int i = 0; i < 8; ++i)
			maxp.u16(0);
		tables["maxp"] = maxp.data;
	}

	{
		// the code page the font serves: the game's own Western, Central European or Cyrillic
		const uint32_t codePage = encoding == "CP1251" ? 0x4u : encoding == "CP1250" ? 0x2u : 0x1u;
		Writer os2;
		os2.u16(3);
		os2.i16(advanceCount ? advanceSum / advanceCount : unitsPerEm / 2);
		os2.u16(400);
		os2.u16(5);
		os2.u16(0);
		os2.i16(unitsPerEm / 2);
		os2.i16(unitsPerEm / 2);
		os2.i16(0);
		os2.i16(unitsPerEm / 8);
		os2.i16(unitsPerEm / 2);
		os2.i16(unitsPerEm / 2);
		os2.i16(0);
		os2.i16(unitsPerEm / 4);
		os2.i16(UNIT);
		os2.i16(ascent * UNIT / 3);
		os2.i16(0);
		for(int i = 0; i < 10; ++i)
			os2.u8(0);
		os2.u32(codePage == 0x4u ? 0x00000203u : 0x00000003u); // Basic Latin, Latin-1, and Cyrillic
		os2.u32(0);
		os2.u32(0);
		os2.u32(0);
		os2.tag("DMB ");
		os2.u16(0x0040); // regular
		os2.u16(static_cast<int>(characters.begin()->first));
		os2.u16(static_cast<int>(characters.rbegin()->first));
		os2.i16(ascent * UNIT);
		os2.i16(-descent * UNIT);
		os2.i16(0);
		os2.u16(std::max(ascent * UNIT, yMax));
		os2.u16(std::max(descent * UNIT, -yMin));
		os2.u32(codePage);
		os2.u32(0);
		os2.i16(ascent * UNIT * 2 / 3);
		os2.i16(ascent * UNIT);
		os2.u16(0);
		os2.u16(32);
		os2.u16(1);
		tables["OS/2"] = os2.data;
	}

	{
		const QString postScript = QString(family).remove(' ');
		const std::vector<std::pair<int, QByteArray>> names = {
			{1, utf16be(family)}, {2, utf16be("Regular")}, {3, utf16be("DMB:" + family)},
			{4, utf16be(family)}, {5, utf16be("Version 1.000")}, {6, utf16be(postScript)},
		};
		Writer name;
		name.u16(0);
		name.u16(static_cast<int>(names.size()));
		name.u16(6 + static_cast<int>(names.size()) * 12);
		int offset = 0;
		for(const auto & [id, text] : names)
		{
			name.u16(3);
			name.u16(1);
			name.u16(0x0409);
			name.u16(id);
			name.u16(text.size());
			name.u16(offset);
			offset += text.size();
		}
		for(const auto & entry : names)
			name.data.append(entry.second);
		tables["name"] = name.data;
	}

	{
		Writer post;
		post.u32(0x00030000);
		post.u32(0);
		post.i16(-UNIT);
		post.i16(UNIT);
		for(int i = 0; i < 5; ++i)
			post.u32(0);
		tables["post"] = post.data;
	}

	// the file: the table directory sorted by tag, each table on a 4-byte boundary
	const int tableCount = static_cast<int>(tables.size());
	Writer font;
	font.u32(0x00010000);
	font.u16(tableCount);
	int power = 1, log2 = 0;
	while(power * 2 <= tableCount)
	{
		power *= 2;
		++log2;
	}
	font.u16(power * 16);
	font.u16(log2);
	font.u16(tableCount * 16 - power * 16);
	uint32_t offset = 12 + tableCount * 16;
	int headAt = 0;
	for(const auto & [tag, data] : tables)
	{
		font.tag(tag.c_str());
		font.u32(checksum(data));
		font.u32(offset);
		font.u32(static_cast<uint32_t>(data.size()));
		if(tag == "head")
			headAt = static_cast<int>(offset);
		offset += (data.size() + 3) & ~3;
	}
	for(const auto & table : tables)
	{
		font.data.append(table.second);
		font.pad();
	}
	const uint32_t adjustment = 0xB1B0AFBAu - checksum(font.data);
	for(int i = 0; i < 4; ++i)
		font.data[headAt + 8 + i] = static_cast<char>((adjustment >> (24 - 8 * i)) & 0xFF);
	return font.data;
}

GameFont::Font GameFont::load(const std::string & name, const QString & family)
{
	static std::map<std::string, Font> loaded;
	if(const auto found = loaded.find(name); found != loaded.end())
		return found->second;

	Font font;
	const ResourcePath path("DATA/" + name, EResType::BMP_FONT);
	if(CResourceHandler::get() && CResourceHandler::get()->existsResource(path))
	{
		const auto data = CResourceHandler::get()->load(path)->readAll();
		const QByteArray fnt(reinterpret_cast<const char *>(data.first.get()), static_cast<int>(data.second));
		std::string encoding = "CP1252";
		try
		{
			encoding = Languages::getLanguageOptions(settings["general"]["gameDataLanguage"].String()).encoding;
		}
		catch(const std::out_of_range &)
		{
			// a data language VCMI does not know: Western, as most copies of the game are
		}
		int height = 0;
		const QByteArray trueTypeFont = trueType(fnt, family, encoding, height);
		const int id = trueTypeFont.isEmpty() ? -1 : QFontDatabase::addApplicationFontFromData(trueTypeFont);
		const QStringList families = id < 0 ? QStringList() : QFontDatabase::applicationFontFamilies(id);
		if(!families.isEmpty())
			font = { families.front(), height };
		logGlobal->info("Launcher font %s from the game's %s: %s", family.toStdString(), name,
			font.family.isEmpty() ? "could not be made" : std::to_string(height) + " pixels high");
	}
	loaded[name] = font;
	return font;
}
