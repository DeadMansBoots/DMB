/*
 * GameLettering.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "GameLettering.h"

#include "CAnimation.h"
#include "Canvas.h"
#include "CanvasImage.h"
#include "IImage.h"
#include "IRenderHandler.h"
#include "IScreenHandler.h"
#include "../GameEngine.h"

#include "../../lib/GameButtonLettering.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/filesystem/Filesystem.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/texts/TextOperations.h"
#include "../../lib/vcmi_endian.h"

namespace
{
// the colours of the words the game carves into its gold buttons (RANSHOW, around its S)
const ColorRGBA CARVED_LETTER(0, 0, 0);
const ColorRGBA CARVED_EDGE(104, 88, 49);
const ColorRGBA CARVED_LIGHT(232, 230, 222);
constexpr double DARK = 0.45; // matches GameButtonLettering's own: a letter's own pixels, at least this black
constexpr int LETTER_GAP = 1;
constexpr int WORD_GAP = 6;
constexpr int CLOSEST = 1;
constexpr int FARTHEST = 5;
}

GameLettering::Letters GameLettering::letters(EFonts font, const std::string & text, bool trim)
{
	// the font's file, by its place in config/fonts.json's list of the game's bitmap fonts, as the
	// renderer loads it (RenderHandler::loadFont)
	const JsonNode config(JsonPath::builtin("config/fonts.json"));
	const auto & names = config["bitmap"].Vector();
	if(static_cast<size_t>(font) >= names.size())
		return {};
	const ResourcePath resource("data/" + names[font].String(), EResType::BMP_FONT);
	if(!CResourceHandler::get()->existsResource(resource))
		return {};
	const auto data = CResourceHandler::get()->load(resource)->readAll();
	const ui8 * bytes = data.first.get();
	const size_t size = data.second;

	// the layout CBitmapFont reads: the height at 5; each glyph's widths before, of and after its pixels
	// at 32; its pixels' offsets after those; the pixels, one byte each, 2 and up a letter's own
	constexpr size_t glyphs = 256;
	constexpr size_t widthsAt = 32;
	constexpr size_t offsetsAt = widthsAt + glyphs * 12;
	constexpr size_t pixelsAt = offsetsAt + glyphs * 4;
	if(size < pixelsAt)
		return {};
	Letters result;
	result.height = bytes[5];

	const std::string encoded = TextOperations::fromUnicode(text, LIBRARY->modh->findResourceEncoding(resource));
	struct Placed
	{
		int x, width;
		const ui8 * pixels;
	};
	std::vector<Placed> placed;
	int x = 0;
	for(const char character : encoded)
	{
		const size_t c = static_cast<ui8>(character);
		const int before = static_cast<int32_t>(read_le_u32(bytes + widthsAt + c * 12));
		const int width = static_cast<int32_t>(read_le_u32(bytes + widthsAt + c * 12 + 4));
		const int after = static_cast<int32_t>(read_le_u32(bytes + widthsAt + c * 12 + 8));
		const size_t offset = pixelsAt + read_le_u32(bytes + offsetsAt + c * 4);
		if(width < 0 || offset + static_cast<size_t>(width) * result.height > size)
			return {};
		placed.push_back({ x + before, width, bytes + offset });
		x += before + width + after;
	}
	result.width = std::max(0, x);
	result.pixels.assign(result.height, std::vector<bool>(result.width, false));
	for(const auto & glyph : placed)
		for(int row = 0; row < result.height; ++row)
			for(int col = 0; col < glyph.width; ++col)
				if(glyph.pixels[row * glyph.width + col] >= 2 && glyph.x + col >= 0 && glyph.x + col < result.width)
					result.pixels[row][glyph.x + col] = true;

	if(!trim)
		return result;
	int left = result.width, right = -1, top = result.height, bottom = -1;
	for(int row = 0; row < result.height; ++row)
		for(int col = 0; col < result.width; ++col)
			if(result.pixels[row][col])
			{
				left = std::min(left, col);
				right = std::max(right, col);
				top = std::min(top, row);
				bottom = std::max(bottom, row);
			}
	if(right < 0)
		return {};
	Letters trimmed;
	trimmed.width = right - left + 1;
	trimmed.height = bottom - top + 1;
	for(int row = top; row <= bottom; ++row)
		trimmed.pixels.emplace_back(result.pixels[row].begin() + left, result.pixels[row].begin() + right + 1);
	return trimmed;
}

std::shared_ptr<IImage> GameLettering::carved(EFonts font, const std::string & text)
{
	const Letters box = letters(font, text, false);
	if(box.width <= 0 || box.height <= 0)
		return nullptr;
	const Point size(box.width + 2, box.height + 2);
	auto image = ENGINE->renderHandler().createImage(size, CanvasScalingPolicy::IGNORE);
	Canvas canvas = image->getCanvas();
	const auto letter = [&box](int col, int row)
	{
		return row >= 0 && row < box.height && col >= 0 && col < box.width && box.pixels[row][col];
	};
	for(int row = -1; row <= box.height; ++row)
		for(int col = -1; col <= box.width; ++col)
		{
			const Point at(col + 1, row + 1);
			if(letter(col, row))
				canvas.drawPoint(at, CARVED_LETTER);
			else if(letter(col - 1, row) || letter(col, row - 1) || letter(col - 1, row - 1))
				canvas.drawPoint(at, CARVED_LIGHT);
			else if(letter(col + 1, row))
				canvas.drawPoint(at, CARVED_EDGE);
		}
	// the game's own art is drawn at the screen's scale the same way
	if(ENGINE->screenHandler().getScalingFactor() > 1)
		image->scaleTo(size, EScalingAlgorithm::XBRZ_ALPHA);
	return image;
}

namespace
{
/// a button's first frame, pixel by pixel, at the game's own size, read through the engine's own
/// renderer (the launcher reads the same buttons a different way: theme.cpp)
GameButtonLettering::Picture readButton(const std::string & name)
{
	GameButtonLettering::Picture picture;
	if(!CResourceHandler::get()->existsResource(AnimationPath::builtin("SPRITES/" + name)))
		return picture;
	const auto frame = ENGINE->renderHandler().loadAnimation(AnimationPath::builtin(name), EImageBlitMode::OPAQUE)->getImage(0);
	if(!frame)
		return picture;
	const Point size = frame->dimensions();
	auto copy = ENGINE->renderHandler().createImage(size, CanvasScalingPolicy::IGNORE);
	Canvas canvas = copy->getCanvas();
	canvas.draw(frame, Point(0, 0));
	picture.w = size.x;
	picture.h = size.y;
	picture.pixels.reserve(size.x * size.y);
	for(int y = 0; y < size.y; ++y)
		for(int x = 0; x < size.x; ++x)
			picture.pixels.push_back(canvas.getPixel(Point(x, y)));
	return picture;
}

/// the lettering, lifted from the player's own files the first time a word asks for it
const GameButtonLettering::Lettering & buttonLettering()
{
	static const GameButtonLettering::Lettering lettering = GameButtonLettering::harvest(readButton);
	return lettering;
}
}

GameLettering::ButtonWords GameLettering::buttonWords(const std::string & text)
{
	using Glyph = GameButtonLettering::Glyph;
	const GameButtonLettering::Lettering & lettering = buttonLettering();
	ButtonWords result;
	if(lettering.band == 0)
		return result;
	// capitals, as the game's buttons write their words
	std::string capitals = boost::algorithm::trim_copy(text);
	for(char & c : capitals)
	{
		if(c == ' ')
			continue;
		c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		if(!lettering.glyphs.count(c))
			return result;
	}
	if(capitals.empty())
		return result;

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
			x += WORD_GAP;
			previous.clear();
			continue;
		}
		const Glyph & glyph = lettering.glyphs.at(c);
		std::map<int, std::pair<int, int>> rows; // its black's first and last column in each row
		for(const auto & [cell, share] : glyph.pixels)
			if(share.black >= DARK)
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
		if(nearest && *nearest < CLOSEST)
			at += CLOSEST - *nearest;
		else if(nearest && *nearest > FARTHEST)
			at -= std::min(*nearest - FARTHEST, 2);
		placed.push_back({&glyph, at});
		previous.clear();
		for(const auto & [row, span] : rows)
			previous[row] = at + span.second;
		x = at + glyph.width + LETTER_GAP;
	}
	const int width = x - LETTER_GAP;

	// every pixel the letters touch, each letter over the one before it where their edges meet
	int minX = std::numeric_limits<int>::max();
	int minY = std::numeric_limits<int>::max();
	int maxX = std::numeric_limits<int>::min();
	int maxY = std::numeric_limits<int>::min();
	for(const auto & p : placed)
		for(const auto & [cell, share] : p.glyph->pixels)
		{
			minX = std::min(minX, p.x + cell.first);
			maxX = std::max(maxX, p.x + cell.first);
			minY = std::min(minY, cell.second);
			maxY = std::max(maxY, cell.second);
		}
	const Point size(maxX - minX + 1, maxY - minY + 1);
	std::vector<double> alpha(size.x * size.y, 0.0);
	std::vector<double> grey(size.x * size.y, 0.0);
	for(const auto & p : placed)
		for(const auto & [cell, share] : p.glyph->pixels)
		{
			const double a = share.black + share.white;
			if(a <= 0)
				continue;
			const double value = 255.0 * share.white / a;
			const size_t at = (cell.second - minY) * size.x + (p.x + cell.first - minX);
			const double under = alpha[at];
			const double both = a + under * (1 - a);
			grey[at] = both > 0 ? (value * a + grey[at] * under * (1 - a)) / both : 0;
			alpha[at] = both;
		}
	auto image = ENGINE->renderHandler().createImage(size, CanvasScalingPolicy::IGNORE);
	Canvas canvas = image->getCanvas();
	for(int row = 0; row < size.y; ++row)
		for(int col = 0; col < size.x; ++col)
		{
			const size_t at = row * size.x + col;
			if(alpha[at] <= 0)
				continue;
			const auto value = static_cast<uint8_t>(std::lround(std::clamp(grey[at], 0.0, 255.0)));
			const auto opacity = static_cast<uint8_t>(std::lround(std::clamp(alpha[at], 0.0, 1.0) * 255));
			// drawPoint blends into what is there; a fill writes the pixel's own opacity
			canvas.drawColor(Rect(col, row, 1, 1), ColorRGBA(value, value, value, opacity));
		}
	if(ENGINE->screenHandler().getScalingFactor() > 1)
		image->scaleTo(size, EScalingAlgorithm::XBRZ_ALPHA);
	result.image = image;
	result.letters = Rect(-minX, -minY, width, lettering.band);
	return result;
}
