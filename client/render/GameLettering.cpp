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

#include "Canvas.h"
#include "CanvasImage.h"
#include "IImage.h"
#include "IRenderHandler.h"
#include "IScreenHandler.h"
#include "../GameEngine.h"

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
