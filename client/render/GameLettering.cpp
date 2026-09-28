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

#include "../../lib/GameLibrary.h"
#include "../../lib/filesystem/Filesystem.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/texts/CGeneralTextHandler.h"
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

namespace
{
/// a button of the game's with words in its gold buttons' lettering, the words, and the letters taken from it
struct WordedButton
{
	std::string name;
	std::string words;
	std::string taken; ///< empty: every letter it has that no button before it gave
};

// the scenario screen's and the game setup's buttons, one cut of the lettering, 22 rows tall. The
// campaign screen's is a little narrower, so only RESTART's R, which no other button has, is taken
// from it. RANSHOW's is a condensed cut, its letters seven tenths as wide, and none are taken.
const std::vector<WordedButton> WORDED_BUTTONS = {
	{"SCNRBEG", "BEGIN", ""}, {"SCNRBACK", "BACK", ""}, {"SCNRLOD", "LOAD", ""}, {"SCNRNEX", "NEXT", ""},
	{"SCNRSAV", "SAVE", ""}, {"SCNREXI", "EXIT", ""}, {"GSPBGIN", "BEGIN", ""}, {"GSPEXIT", "EXIT", ""},
	{"CAMPRST", "RESTART", "R"}};
// a letter found again on another button must be the same letter: its black over the first's, a pixel
// either way at most, at least half of the two together (a mis-cut letter or another word scores far less)
constexpr double SAME_LETTER = 0.5;

constexpr double DARK = 0.45;  // a letter's own pixels: at least this much black
constexpr double FAINT = 0.10; // less of black or white than this is the gold's own grain
// the game's spacing, measured on its buttons: its letters stand a pixel apart box to box, its words
// eight black to black; no two letters' black nearer than a pixel in any row, and a pair farther
// apart than five in every row (T A, O A) drawn in towards it, by two at most
constexpr int LETTER_GAP = 1;
constexpr int WORD_GAP = 6;
constexpr int CLOSEST = 1;
constexpr int FARTHEST = 5;

/// how much of a pixel is black and how much white, over the bar's gold
struct Share
{
	double black = 0;
	double white = 0;
};

using Cell = std::pair<int, int>; ///< column, row
using Pixels = std::map<Cell, Share>;

struct Glyph
{
	Pixels pixels; ///< columns from its black's left edge, rows from the lettering's top row
	int width = 0; ///< of its black
};

struct ButtonLettering
{
	int band = 0; ///< the letters' height, from the top of their black to its foot
	std::map<char, Glyph> glyphs;
};

double luminance(const ColorRGBA & c)
{
	return (c.r * 299 + c.g * 587 + c.b * 114) / 1000.0;
}

int floorHalf(int value)
{
	return value >= 0 ? value / 2 : -((1 - value) / 2);
}

/// a button's first frame, pixel by pixel, at the game's own size
struct Picture
{
	int w = 0;
	int h = 0;
	std::vector<ColorRGBA> pixels;
	const ColorRGBA & at(int x, int y) const { return pixels[y * w + x]; }
};

Picture readButton(const std::string & name)
{
	Picture picture;
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

/// the gold face between the frame's inner lines, [left, right): the innermost column in each end
/// quarter that is the frame's mid-brown line top to bottom. The scenario buttons have scrollwork ends,
/// the campaign ones a plain frame.
std::pair<int, int> faceOf(const Picture & p)
{
	const auto line = [&p](int x)
	{
		int count = 0;
		for(int y = 5; y < p.h - 5; ++y)
		{
			const double l = luminance(p.at(x, y));
			if(l >= 60 && l <= 135)
				++count;
		}
		return count >= 0.85 * (p.h - 10);
	};
	int left = 20;
	for(int x = 0; x < p.w / 4; ++x)
		if(line(x))
			left = x;
	int right = p.w - 21;
	for(int x = p.w - p.w / 4; x < p.w; ++x)
		if(line(x))
		{
			right = x;
			break;
		}
	return {left + 1, right};
}

/// `px` as white and gold mixed, the rest black: px = white * w + gold * g, least squares over the
/// three channels
Share unmix(const ColorRGBA & px, const std::array<double, 3> & gold)
{
	const double ww = 3 * 255.0 * 255.0;
	const double wg = 255.0 * (gold[0] + gold[1] + gold[2]);
	const double gg = gold[0] * gold[0] + gold[1] * gold[1] + gold[2] * gold[2];
	const double wp = 255.0 * (px.r + px.g + px.b);
	const double gp = gold[0] * px.r + gold[1] * px.g + gold[2] * px.b;
	const double det = ww * gg - wg * wg;
	if(std::abs(det) < 1e-6)
		return {};
	double white = std::clamp((wp * gg - gp * wg) / det, 0.0, 1.0);
	double inGold = std::clamp((gp * ww - wp * wg) / det, 0.0, 1.0);
	if(white + inGold > 1)
	{
		const double sum = white + inGold;
		white /= sum;
		inGold /= sum;
	}
	return {1 - white - inGold, white};
}

/// every pixel of the face as black and white over the bar's gold. The gold under a letter is its
/// row's gold on either side of the letter, three pixels of each, the nearer side weighted more: the
/// bar is brushed metal, streaked along its length.
std::vector<Share> layersOf(const Picture & p, int left, int right)
{
	const int w = p.w;
	const int h = p.h;
	std::vector<bool> touched(w * h);
	for(int y = 0; y < h; ++y)
		for(int x = 0; x < w; ++x)
		{
			const auto & c = p.at(x, y);
			const int saturation = std::max({c.r, c.g, c.b}) - std::min({c.r, c.g, c.b});
			touched[y * w + x] = !(luminance(c) > 110 && saturation > 45);
		}
	std::vector<bool> grown(w * h);
	for(int y = 0; y < h; ++y)
		for(int x = 0; x < w; ++x)
			for(int yy = std::max(0, y - 1); yy < std::min(h, y + 2); ++yy)
				for(int xx = std::max(0, x - 1); xx < std::min(w, x + 2); ++xx)
					if(touched[yy * w + xx])
						grown[y * w + x] = true;

	std::vector<Share> result(w * h);
	for(int y = 0; y < h; ++y)
		for(int x = left; x < right; ++x)
		{
			if(!grown[y * w + x])
				continue;
			std::array<double, 3> leftSum = {0, 0, 0};
			std::array<double, 3> rightSum = {0, 0, 0};
			int leftCount = 0;
			int rightCount = 0;
			int leftDistance = 0;
			int rightDistance = 0;
			for(int xx = x - 1; xx >= left && leftCount < 3; --xx)
				if(!grown[y * w + xx])
				{
					if(leftCount == 0)
						leftDistance = x - xx;
					const auto & c = p.at(xx, y);
					leftSum[0] += c.r;
					leftSum[1] += c.g;
					leftSum[2] += c.b;
					++leftCount;
				}
			for(int xx = x + 1; xx < right && rightCount < 3; ++xx)
				if(!grown[y * w + xx])
				{
					if(rightCount == 0)
						rightDistance = xx - x;
					const auto & c = p.at(xx, y);
					rightSum[0] += c.r;
					rightSum[1] += c.g;
					rightSum[2] += c.b;
					++rightCount;
				}
			if(leftCount == 0 && rightCount == 0)
				continue;
			std::array<double, 3> gold;
			for(int i = 0; i < 3; ++i)
			{
				if(leftCount && rightCount)
				{
					const double t = leftDistance / static_cast<double>(leftDistance + rightDistance);
					gold[i] = leftSum[i] / leftCount * (1 - t) + rightSum[i] / rightCount * t;
				}
				else
					gold[i] = leftCount ? leftSum[i] / leftCount : rightSum[i] / rightCount;
			}
			Share share = unmix(p.at(x, y), gold);
			if(share.black < FAINT)
				share.black = 0;
			if(share.white < FAINT)
				share.white = 0;
			result[y * w + x] = share;
		}
	return result;
}

struct Group
{
	int x0 = 0;
	int x1 = 0;
	std::vector<Cell> cells;
};

Group joined(const Group & a, const Group & b)
{
	Group result{std::min(a.x0, b.x0), std::max(a.x1, b.x1), a.cells};
	result.cells.insert(result.cells.end(), b.cells.begin(), b.cells.end());
	return result;
}

/// the button's word's letters, each as its black and white; false when the black's pieces cannot be
/// matched to the word's letters. `known`, the letters other buttons gave, places the cut between two
/// that touch.
bool lettersOf(const Picture & p, const std::string & word, const std::map<char, Glyph> & known, std::map<char, Glyph> & glyphs, int & band)
{
	const auto [left, right] = faceOf(p);
	const int w = p.w;
	const int h = p.h;
	const auto layers = layersOf(p, left, right);
	const auto black = [&](int x, int y) { return layers[y * w + x].black; };

	// the letters' black in 8-connected pieces
	std::vector<std::vector<Cell>> pieces;
	std::vector<bool> seen(w * h);
	for(int y = 0; y < h; ++y)
		for(int x = left; x < right; ++x)
		{
			if(seen[y * w + x] || black(x, y) < DARK)
				continue;
			std::vector<Cell> stack = {{x, y}};
			std::vector<Cell> cells;
			seen[y * w + x] = true;
			while(!stack.empty())
			{
				const Cell c = stack.back();
				stack.pop_back();
				cells.push_back(c);
				for(int nx = c.first - 1; nx <= c.first + 1; ++nx)
					for(int ny = c.second - 1; ny <= c.second + 1; ++ny)
						if(nx >= left && nx < right && ny >= 0 && ny < h && !seen[ny * w + nx] && black(nx, ny) >= DARK)
						{
							seen[ny * w + nx] = true;
							stack.emplace_back(nx, ny);
						}
			}
			if(cells.size() >= 3)
				pieces.push_back(cells);
		}
	const auto firstColumn = [](const std::vector<Cell> & cells)
	{
		return std::min_element(cells.begin(), cells.end())->first;
	};
	std::stable_sort(pieces.begin(), pieces.end(), [&](const auto & a, const auto & b) { return firstColumn(a) < firstColumn(b); });

	// pieces over each other's columns are one letter a hairline broke in two
	std::vector<Group> groups;
	for(const auto & cells : pieces)
	{
		Group piece{firstColumn(cells), std::max_element(cells.begin(), cells.end())->first, cells};
		if(!groups.empty())
		{
			const Group & last = groups.back();
			const int overlap = std::min(last.x1, piece.x1) - std::max(last.x0, piece.x0) + 1;
			if(overlap >= std::min(piece.x1 - piece.x0 + 1, last.x1 - last.x0 + 1) * 0.5 || (cells.size() < 25 && piece.x0 - last.x1 <= 1))
			{
				groups.back() = joined(last, piece);
				continue;
			}
		}
		groups.push_back(piece);
	}

	std::string letters;
	for(const char c : word)
		if(c != ' ')
			letters += c;
	// small leftovers join the nearer neighbour
	while(groups.size() > letters.size() && groups.size() > 1)
	{
		size_t i = 0;
		for(size_t j = 1; j < groups.size(); ++j)
			if(groups[j].cells.size() < groups[i].cells.size())
				i = j;
		const int leftGap = i > 0 ? groups[i].x0 - groups[i - 1].x1 : std::numeric_limits<int>::max();
		const int rightGap = i + 1 < groups.size() ? groups[i + 1].x0 - groups[i].x1 : std::numeric_limits<int>::max();
		const size_t a = leftGap <= rightGap ? i - 1 : i;
		groups[a] = joined(groups[a], groups[a + 1]);
		groups.erase(groups.begin() + a + 1);
	}
	// letters that touch come out as one piece: the widest is cut along the path of least black down its
	// rows, a column either way per row, so kerned diagonals (A V) part along their gap. Where one of the
	// two is known from another button, the cut keeps within three columns of where its width ends: SAVE's
	// V is held to its E by their top serifs, and to itself only by a hairline, which the least black
	// alone would cut instead.
	while(groups.size() < letters.size())
	{
		size_t i = 0;
		for(size_t j = 1; j < groups.size(); ++j)
			if(groups[j].x1 - groups[j].x0 > groups[i].x1 - groups[i].x0)
				i = j;
		const Group group = groups[i];
		if(group.x1 - group.x0 < 16)
			break;
		int y0 = h;
		int y1 = -1;
		for(const auto & c : group.cells)
		{
			y0 = std::min(y0, c.second);
			y1 = std::max(y1, c.second);
		}
		int c0 = group.x0 + 4;
		int c1 = group.x1 - 4;
		std::optional<int> expected;
		if(known.count(letters[i]))
			expected = group.x0 + known.at(letters[i]).width;
		else if(i + 1 < letters.size() && known.count(letters[i + 1]))
			expected = group.x1 - known.at(letters[i + 1]).width + 1;
		if(expected && std::max(c0, *expected - 3) <= std::min(c1, *expected + 3))
		{
			c0 = std::max(c0, *expected - 3);
			c1 = std::min(c1, *expected + 3);
		}
		if(c1 < c0)
			break;
		const int columns = c1 - c0 + 1;
		std::vector<double> cost((y1 - y0 + 1) * columns);
		std::vector<int> back((y1 - y0 + 1) * columns);
		for(int c = c0; c <= c1; ++c)
			cost[c - c0] = black(c, y0);
		for(int y = y0 + 1; y <= y1; ++y)
			for(int c = c0; c <= c1; ++c)
			{
				int previous = -1;
				for(int pc = c - 1; pc <= c + 1; ++pc)
					if(pc >= c0 && pc <= c1 && (previous < 0 || cost[(y - 1 - y0) * columns + pc - c0] < cost[(y - 1 - y0) * columns + previous - c0]))
						previous = pc;
				cost[(y - y0) * columns + c - c0] = cost[(y - 1 - y0) * columns + previous - c0] + black(c, y);
				back[(y - y0) * columns + c - c0] = previous;
			}
		int c = c0;
		for(int cc = c0 + 1; cc <= c1; ++cc)
			if(cost[(y1 - y0) * columns + cc - c0] < cost[(y1 - y0) * columns + c - c0])
				c = cc;
		std::vector<int> seam(y1 - y0 + 1);
		for(int y = y1; y >= y0; --y)
		{
			seam[y - y0] = c;
			if(y > y0)
				c = back[(y - y0) * columns + c - c0];
		}
		Group before{std::numeric_limits<int>::max(), -1, {}};
		Group after{std::numeric_limits<int>::max(), -1, {}};
		for(const auto & cell : group.cells)
		{
			Group & side = cell.first < seam[cell.second - y0] ? before : after;
			side.cells.push_back(cell);
			side.x0 = std::min(side.x0, cell.first);
			side.x1 = std::max(side.x1, cell.first);
		}
		if(before.cells.empty() || after.cells.empty())
			break;
		groups[i] = before;
		groups.insert(groups.begin() + i + 1, after);
	}
	if(groups.size() != letters.size())
		return false;

	// the lettering's rows: those the letters' black spans
	int top = h;
	int bottom = -1;
	std::vector<int> owner(w * h, -1);
	for(size_t i = 0; i < groups.size(); ++i)
		for(const auto & cell : groups[i].cells)
		{
			owner[cell.second * w + cell.first] = static_cast<int>(i);
			top = std::min(top, cell.second);
			bottom = std::max(bottom, cell.second);
		}
	// each pixel of white, or of black too faint to be a letter's own, goes to the letter whose black is
	// nearest, two pixels away at most
	const auto nearest = [&](int x, int y)
	{
		for(int d = 1; d <= 2; ++d)
			for(int yy = y - d; yy <= y + d; ++yy)
				for(int xx = x - d; xx <= x + d; ++xx)
					if((std::abs(yy - y) == d || std::abs(xx - x) == d) && xx >= 0 && xx < w && yy >= 0 && yy < h && owner[yy * w + xx] >= 0)
						return owner[yy * w + xx];
		return -1;
	};
	std::map<char, Glyph> found;
	for(size_t i = 0; i < groups.size(); ++i)
	{
		const Group & group = groups[i];
		Glyph glyph;
		glyph.width = group.x1 - group.x0 + 1;
		for(int y = std::max(0, top - 3); y < std::min(h, bottom + 4); ++y)
			for(int x = std::max(left, group.x0 - 3); x < std::min(right, group.x1 + 4); ++x)
			{
				const Share & share = layers[y * w + x];
				if(share.black == 0 && share.white == 0)
					continue;
				const int whose = owner[y * w + x] >= 0 ? owner[y * w + x] : nearest(x, y);
				if(whose == static_cast<int>(i))
					glyph.pixels[{x - group.x0, y - top}] = share;
			}
		found.emplace(letters[i], glyph);
	}
	glyphs = found;
	band = bottom - top + 1;
	return true;
}

std::map<int, std::vector<int>> inkRows(const Glyph & glyph)
{
	std::map<int, std::vector<int>> rows;
	for(const auto & [cell, share] : glyph.pixels)
		if(share.black >= DARK)
			rows[cell.second].push_back(cell.first);
	for(auto & row : rows)
		std::sort(row.second.begin(), row.second.end());
	return rows;
}

bool inked(const std::map<int, std::vector<int>> & rows, int row, int column)
{
	const auto it = rows.find(row);
	return it != rows.end() && std::binary_search(it->second.begin(), it->second.end(), column);
}

/// the first run of columns black in most of the letter's rows: its stem (R's bowl and leg stand in most
/// rows too, to the right of it)
std::pair<int, int> stemOf(const Glyph & glyph, int band)
{
	const auto rows = inkRows(glyph);
	std::vector<int> columns;
	for(int x = -2; x < glyph.width + 2; ++x)
	{
		int count = 0;
		for(int y = 0; y < band; ++y)
			if(inked(rows, y, x))
				++count;
		if(count >= band * 0.75)
			columns.push_back(x);
	}
	if(columns.empty())
		return {0, 0};
	int right = columns.front();
	while(std::find(columns.begin(), columns.end(), right + 1) != columns.end())
		++right;
	return {columns.front(), right};
}

Pixels part(const Glyph & glyph, const std::function<bool(int x, int y)> & keep)
{
	Pixels result;
	for(const auto & [cell, share] : glyph.pixels)
		if(keep(cell.first, cell.second))
			result[cell] = share;
	return result;
}

Pixels shifted(const Pixels & pixels, int dx)
{
	Pixels result;
	for(const auto & [cell, share] : pixels)
		result[{cell.first + dx, cell.second}] = share;
	return result;
}

/// later layers over earlier ones, keeping the darker where two letters' black meet
Pixels merged(const std::vector<Pixels> & layers)
{
	Pixels result;
	for(const auto & layer : layers)
		for(const auto & [cell, share] : layer)
		{
			const auto it = result.find(cell);
			if(it != result.end() && it->second.black > share.black)
				continue;
			result[cell] = share;
		}
	return result;
}

/// the pixels as a letter: moved so its black starts at column 0
Glyph normalised(const Pixels & pixels)
{
	int left = std::numeric_limits<int>::max();
	int right = std::numeric_limits<int>::min();
	for(const auto & [cell, share] : pixels)
		if(share.black >= DARK)
		{
			left = std::min(left, cell.first);
			right = std::max(right, cell.first);
		}
	Glyph glyph;
	if(right < left)
		return glyph;
	glyph.pixels = shifted(pixels, -left);
	glyph.width = right - left + 1;
	return glyph;
}

/// the letters no button has, built from the strokes of those they do: each is the game's own pixels,
/// cut and set together
void buildMissing(std::map<char, Glyph> & g, int band)
{
	const int foot = band - 3; // from here down, a stem's foot serif
	const auto has = [&g](const std::string & letters)
	{
		return std::all_of(letters.begin(), letters.end(), [&g](char c) { return g.count(c) > 0; });
	};
	std::map<char, Glyph> made;
	if(has("EI"))
	{
		// F: E above its lower arm's upturned serif, E's stem and middle arm below that, I's foot
		const Glyph & e = g.at('E');
		const Glyph & i = g.at('I');
		const auto rows = inkRows(e);
		const int eStem = stemOf(e, band).first;
		const int iStem = stemOf(i, band).first;
		int serifTop = foot;
		const auto serifIn = [&](int row)
		{
			const auto it = rows.find(row);
			return it != rows.end() && std::any_of(it->second.begin(), it->second.end(), [&](int x) { return x >= e.width - 6; });
		};
		while(serifTop > band / 2 && serifIn(serifTop - 1))
			--serifTop;
		int armRight = 0;
		for(int y = band / 3; y < 2 * band / 3; ++y)
			if(rows.count(y))
				for(int x : rows.at(y))
					if(x < e.width - 6)
						armRight = std::max(armRight, x);
		const Pixels upper = part(e, [&](int x, int y) { return y < serifTop || (y < foot && x <= armRight + 2); });
		const Pixels lower = shifted(part(i, [&](int, int y) { return y >= foot; }), eStem - iStem);
		made['F'] = normalised(merged({upper, lower}));
	}
	if(has("IE"))
	{
		// H: two I's eight pixels apart, E's middle arm between them
		const Glyph & i = g.at('I');
		const Glyph & e = g.at('E');
		const auto [iLeft, iRight] = stemOf(i, band);
		const int eRight = stemOf(e, band).second;
		const auto rows = inkRows(e);
		const int d = iRight + 9 - iLeft;
		std::vector<int> arm;
		for(int y = band / 3; y < 2 * band / 3; ++y)
			if(inked(rows, y, eRight + 1) && inked(rows, y, eRight + 2) && inked(rows, y, eRight + 3))
				arm.push_back(y);
		Pixels bar;
		if(!arm.empty())
			for(int y = arm.front() - 1; y < arm.back() + 3; ++y)
			{
				const auto it = e.pixels.find({eRight + 3, y});
				if(it != e.pixels.end())
					for(int x = iRight + 1; x < d + iLeft; ++x)
						bar[{x, y}] = it->second;
			}
		made['H'] = normalised(merged({bar, i.pixels, shifted(i.pixels, d)}));
	}
	if(has("R"))
	{
		// P: R without its leg
		const Glyph & r = g.at('R');
		const auto rows = inkRows(r);
		const int stemRight = stemOf(r, band).second;
		int bowl = band / 2;
		bool joins = false;
		for(int y = band / 4; y < 2 * band / 3; ++y)
			if(rows.count(y) && std::any_of(rows.at(y).begin(), rows.at(y).end(), [&](int x) { return x > stemRight && x <= stemRight + 3; }))
			{
				bowl = joins ? std::max(bowl, y) : y;
				joins = true;
			}
		int footRight = stemRight;
		if(rows.count(band - 1))
			for(int x : rows.at(band - 1))
				if(x <= stemRight + 5)
					footRight = std::max(footRight, x);
		made['P'] = normalised(part(r, [&](int x, int y) { return y <= bowl || x <= stemRight + 3 || (y >= band - 2 && x <= footRight + 1); }));
	}
	if(has("XI"))
	{
		// Y: X's arms down to where they cross, I's stem and foot from there
		const Glyph & x = g.at('X');
		const Glyph & i = g.at('I');
		const auto rows = inkRows(x);
		const auto [iLeft, iRight] = stemOf(i, band);
		int cross = -1;
		for(int y = band / 4; y < 2 * band / 3; ++y)
		{
			const auto it = rows.find(y);
			if(it == rows.end())
				continue;
			const auto & cols = it->second;
			// one run of black only
			if(cols.back() - cols.front() + 1 != static_cast<int>(cols.size()))
				continue;
			if(cross < 0 || cols.back() - cols.front() < rows.at(cross).back() - rows.at(cross).front())
				cross = y;
		}
		if(cross >= 0)
		{
			const int dx = floorHalf(rows.at(cross).front() + rows.at(cross).back() - (iLeft + iRight));
			const Pixels arms = part(x, [&](int, int y) { return y < cross; });
			const Pixels stem = shifted(part(i, [&](int, int y) { return y >= cross - 1; }), dx);
			made['Y'] = normalised(merged({arms, stem}));
		}
	}
	if(has("OI"))
	{
		// U: O's sides carried straight up from its middle, I's top serifs on them
		const Glyph & o = g.at('O');
		const Glyph & i = g.at('I');
		const auto rows = inkRows(o);
		const auto [iLeft, iRight] = stemOf(i, band);
		const int turn = band / 2;
		std::vector<int> leftSide;
		std::vector<int> rightSide;
		if(rows.count(turn))
			for(int x : rows.at(turn))
				(x < o.width / 2 ? leftSide : rightSide).push_back(x);
		if(!leftSide.empty() && !rightSide.empty())
		{
			Pixels sides;
			for(int y = 0; y < turn; ++y)
				for(int x = -1; x < o.width + 1; ++x)
				{
					const auto it = o.pixels.find({x, turn});
					if(it != o.pixels.end() && (x <= leftSide.back() + 2 || x >= rightSide.front() - 1))
						sides[{x, y}] = it->second;
				}
			Pixels tops;
			for(const auto * side : {&leftSide, &rightSide})
			{
				const int dx = floorHalf(side->front() + side->back()) - floorHalf(iLeft + iRight);
				for(const auto & [cell, share] : part(i, [](int, int y) { return y < 3; }))
					tops[{cell.first + dx, cell.second}] = share;
			}
			made['U'] = normalised(merged({sides, part(o, [&](int, int y) { return y >= turn; }), tops}));
		}
	}
	if(has("VNI"))
	{
		const Glyph & v = g.at('V');
		const Glyph & n = g.at('N');
		const Glyph & i = g.at('I');
		const auto rows = inkRows(v);
		if(rows.count(1) && rows.count(3))
		{
			// M: N's thin left stem, V's arms inside it without their top serifs, I's thick stem on the right
			const int nRight = stemOf(n, band).second;
			const int iLeft = stemOf(i, band).first;
			const int dv = nRight + 1 - rows.at(3).front();
			const int di = rows.at(3).back() + dv - 1 - iLeft;
			const Pixels thin = part(n, [&](int x, int) { return x <= nRight + 1; });
			const Pixels arms = shifted(part(v, [](int, int y) { return y >= 2; }), dv);
			made['M'] = normalised(merged({thin, arms, shifted(i.pixels, di)}));
			// W: two V's, the second's left serif over the first's right one
			const auto serif = std::find_if(rows.at(1).begin(), rows.at(1).end(), [&v](int x) { return x > v.width / 2; });
			if(serif != rows.at(1).end())
				made['W'] = normalised(merged({v.pixels, shifted(v.pixels, *serif - 1)}));
		}
	}
	for(auto & [letter, glyph] : made)
		if(glyph.width > 0)
			g.emplace(letter, glyph);
}

/// how much of two letters' black is the same, the second moved a pixel either way at most
double overlap(const Glyph & a, const Glyph & b)
{
	std::set<Cell> first;
	std::set<Cell> second;
	for(const auto & [cell, share] : a.pixels)
		if(share.black >= DARK)
			first.insert(cell);
	for(const auto & [cell, share] : b.pixels)
		if(share.black >= DARK)
			second.insert(cell);
	double best = 0;
	for(int dx = -1; dx <= 1; ++dx)
		for(int dy = -1; dy <= 1; ++dy)
		{
			size_t both = 0;
			for(const auto & cell : second)
				if(first.count({cell.first + dx, cell.second + dy}))
					++both;
			const size_t either = first.size() + second.size() - both;
			if(either)
				best = std::max(best, static_cast<double>(both) / either);
		}
	return best;
}

/// the lettering, lifted from the player's own files the first time a word asks for it
const ButtonLettering & buttonLettering()
{
	static const ButtonLettering lettering = []()
	{
		ButtonLettering result;
		// other languages' buttons carry other words, the game's own or a translation's
		if(CGeneralTextHandler::getInstalledLanguage() != "english" || CGeneralTextHandler::getPreferredLanguage() != "english")
			return result;
		for(const auto & button : WORDED_BUTTONS)
		{
			const Picture picture = readButton(button.name);
			std::map<char, Glyph> found;
			int band = 0;
			if(picture.w == 0 || !lettersOf(picture, button.words, result.glyphs, found, band))
			{
				logGlobal->debug("Gold button lettering: the words of %s could not be read", button.name);
				continue;
			}
			if(result.band == 0)
				result.band = band;
			// letters of another height are another lettering
			if(band != result.band)
				continue;
			for(auto & [letter, glyph] : found)
			{
				const auto known = result.glyphs.find(letter);
				if(known != result.glyphs.end())
				{
					if(overlap(known->second, glyph) < SAME_LETTER)
					{
						logGlobal->warn("Gold button lettering: %s's %s is not the one other buttons have; the font is used instead", button.name, std::string(1, letter));
						return ButtonLettering();
					}
					continue;
				}
				if(!button.taken.empty() && button.taken.find(letter) == std::string::npos)
					continue;
				result.glyphs.emplace(letter, glyph);
			}
		}
		if(result.band == 0)
			return ButtonLettering();
		buildMissing(result.glyphs, result.band);
		return result;
	}();
	return lettering;
}
}

GameLettering::ButtonWords GameLettering::buttonWords(const std::string & text)
{
	const ButtonLettering & lettering = buttonLettering();
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
