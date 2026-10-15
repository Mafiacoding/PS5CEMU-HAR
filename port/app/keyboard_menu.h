// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: the in-game keyboards, the 3DS's (ingame3ds.cpp) and the Wii U's (ingame.cpp, which
// Cemu's swkbd.cpp hands its keyboard to, patches/cemu). Over the game, on the launcher's 1920x1080
// as the menus are: what the game asks for, the text, the menus' language's keys (app/keyboard.h,
// whose pages L1 and R1 go through), then the game's own buttons, the last of which confirms.

#pragma once

#include "keyboard.h"
#include "lang.h"
#include "menu_canvas.h"
#include "../ps5/pad.h"
#include "../ui/text.h"

#include <fmt/format.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ps5menu
{
	struct KeyboardAsk
	{
		std::string hint;				  // what the game asks for; may be empty
		std::string error;				  // why the game refused what was typed; may be empty
		int maxLength = -1;				  // the most the text may have, in UTF-16 units as the 3DS and the Wii U count; -1: none
		bool counted = true;			  // its length shown against maxLength
		std::vector<std::string> buttons; // the game's, left to right; the last one confirms. None: OK
	};

	class KeyboardSheet
	{
	public:
		std::string typed;

		// A new keyboard: the text the game gives, the language's first page
		void Start(const std::string& text)
		{
			typed = text;
			m_shift = false;
			m_page = ps5keyboard::FirstPage(ps5lang::Current(), false, true);
			m_focusRow = -1;
			m_refocus = false;
		}

		// Drawn as a window over the screen; then the controller's buttons pressed this frame (held: down):
		// Circle and Square delete, Triangle shifts, L1 and R1 change the page and Options confirms with
		// the last button, not while the touchpad is held for a shortcut. Returns the button chosen, or -1.
		int Draw(const char* window, float scale, const Fonts& fonts, const Palette& colours, const KeyboardAsk& ask, uint32_t pressed,
			uint32_t held)
		{
			using ps5lang::Tr;
			std::vector<std::string> buttons = ask.buttons;
			if (buttons.empty())
				buttons = {Tr("OK")};
			int chosen = -1;
			ImGuiIO& io = ImGui::GetIO();
			const ImVec2 origin{(io.DisplaySize.x - 1920.0f * scale) * 0.5f, (io.DisplaySize.y - 1080.0f * scale) * 0.5f};
			ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
			ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
			ImGui::SetNextWindowFocus();
			ImGui::PushStyleColor(ImGuiCol_NavHighlight, IM_COL32(0, 0, 0, 0));
			constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
				ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse;
			if (ImGui::Begin(window, nullptr, kFlags))
			{
				const bool appearing = ImGui::IsWindowAppearing();
				const Canvas canvas{ImGui::GetWindowDrawList(), scale, origin, colours};
				canvas.draw->AddRectFilled({0, 0}, io.DisplaySize, colours.dim);
				canvas.Panel(260, 110, 1400, 860);
				// tr: over the in-game keyboard: what the game asks to have typed
				canvas.Text(fonts.caption, 20, 300, 136, colours.kicker, ui::Upper(Tr("The game asks for"), ps5lang::Current() == "tr"));
				canvas.Text(fonts.heading, 32, 300, 170, colours.title, ask.hint.empty() ? Tr("Some text") : ask.hint, 1320);
				canvas.Row(300, 236, 1320, 72, false);
				float textRight = 1590;
				if (ask.counted && ask.maxLength >= 0)
				{
					const std::string count = fmt::format("{} / {}", ps5keyboard::Utf16Length(typed), ask.maxLength);
					canvas.TextRight(fonts.caption, 20, 1590, 262, colours.accent, count);
					textRight -= canvas.Width(fonts.caption, 20, count) + 24;
				}
				canvas.Text(fonts.label, 24, 330, 258, colours.text, Tail(canvas, fonts.label, typed + "_", textRight - 330));
				if (!ask.error.empty())
					canvas.Text(fonts.caption, 20, 300, 320, colours.accent, ask.error, 1320);

				const std::vector<ps5keyboard::Page>& pages = ps5keyboard::Pages(ps5lang::Current(), true);
				m_page = std::clamp(m_page, 0, (int)pages.size() - 1);
				const ps5keyboard::Page& page = pages[m_page];
				// the page L1 and R1 go to next, by the letters it has ("abc", "äöü", "абв", "#+=")
				canvas.TextRight(fonts.caption, 20, 1620, 136, colours.kicker, ps5lang::TrF("L1 R1: {0}", pages[(m_page + 1) % pages.size()].label));

				// a key: true when chosen (Cross, or a touch of the touchpad's click). Its ID is its place's,
				// so the focus stays where it was when Shift changes the keys
				bool first = true;
				auto key = [&](const std::string& id, float x, float y, float width, float height, const std::string& label, bool focus) {
					ImGui::SetCursorScreenPos(canvas.At(x, y));
					const bool chosen = ImGui::InvisibleButton(("##" + id).c_str(), {width * scale, height * scale});
					if ((first && appearing) || focus)
					{
						// the controller's focus, which Cross types at once (SetFocusID leaves it to a mouse,
						// and ImGui to a D-pad move first)
						ImGui::SetFocusID(ImGui::GetItemID(), ImGui::GetCurrentWindow());
						ImGui::GetCurrentContext()->NavDisableHighlight = false;
					}
					first = false;
					canvas.Row(x, y, width, height, ImGui::IsItemFocused());
					// as the launcher's keys have them; a longer language's word a little smaller, never out of its key
					canvas.TextFit(fonts.body, 26, x + width / 2, y + (height - 30) / 2, colours.text, label, width - 16, Canvas::Align::Centre);
					return chosen;
				};
				// as many rows as the language's tallest page has, five lower than four; the keys as wide as
				// its longest row lets ten keys' width hold them, each row centred
				const int tallest = ps5keyboard::MostRows(ps5lang::Current(), true);
				const int longest = std::max(10, ps5keyboard::MostKeys(ps5lang::Current(), true));
				const float height = tallest > 4 ? 60.0f : 72.0f, pitch = height + 12;
				const float keyWidth = std::floor((1270 - 10.0f * (longest - 1)) / longest);
				const bool circle = fonts.body->FindGlyphNoFallback(0x25cc) != nullptr;
				const int rows = (int)page.rows.size();
				int focusedRow = -1, focusedColumn = 0;
				for (int row = 0; row < rows; row++)
				{
					const int keys = (int)page.rows[row].size();
					const float left = 325 + (1270 - (keys * keyWidth + (keys - 1) * 10.0f)) / 2;
					for (int column = 0; column < keys; column++)
					{
						const ps5keyboard::Key& k = page.rows[row][column];
						// L1 or R1 changed the page: the focus where it was, or as near as this page's keys go
						const bool refocus = m_refocus && row == std::min(m_focusRow, rows - 1) && column == std::min(m_focusColumn, keys - 1);
						std::string label = ps5keyboard::Label(k, m_shift);
						if (!circle && label.rfind("◌", 0) == 0)
							label.erase(0, 3); // a mark alone, where the font has no dotted circle to put it on
						if (key(fmt::format("key{}", row * 16 + column), left + column * (keyWidth + 10), 360 + row * pitch, keyWidth, height, label,
								refocus))
							Type(ps5keyboard::Typed(typed, page, k, m_shift), ask);
						if (ImGui::IsItemFocused())
						{
							focusedRow = row;
							focusedColumn = column;
						}
					}
				}
				m_focusRow = focusedRow;
				m_focusColumn = focusedColumn;
				m_refocus = false;
				const float actionY = 360 + tallest * pitch;
				// tr: the in-game keyboard's keys
				if (key("shift", 325, actionY, 246, height, m_shift ? Tr("Shift: on") : Tr("Shift"), false))
					m_shift = !m_shift;
				if (key("space", 581, actionY, 502, height, Tr("Space"), false))
					Type(typed + " ", ask);
				if (key("delete", 1093, actionY, 502, height, Tr("Delete"), false))
					typed = ps5keyboard::Deleted(typed);
				// the game's buttons, right-aligned on the last row
				const float buttonWidth = 300, buttonY = actionY + pitch + 20;
				const int count = (int)buttons.size();
				for (int button = 0; button < count; button++)
				{
					const float x = 1595 - (count - button) * (buttonWidth + 10) + 10;
					if (key(fmt::format("button{}", button), x, buttonY, buttonWidth, height, buttons[button], false))
						chosen = button;
				}

				canvas.draw->AddLine(canvas.At(300, 900), canvas.At(1620, 900), colours.line, scale);
				float x = 300;
				const std::vector<std::pair<const char*, std::string>> hints = {
					// tr: the in-game keyboard's hints: Cross types the focused key
					{"cross", Tr("Type")}, {"circle", Tr("Delete")}, {"triangle", Tr("Shift")}, {"options", buttons.back()}};
				const float hintSize = canvas.HintsSize(fonts.caption, hints, 1320);
				for (const auto& [button, label] : hints)
					x = canvas.Hint(fonts.caption, x, 915, button, label, hintSize);
			}
			ImGui::End();
			ImGui::PopStyleColor();

			if (!(held & ps5pad::kTouchPad))
			{
				if (pressed & (ps5pad::kCircle | ps5pad::kSquare))
					typed = ps5keyboard::Deleted(typed);
				if (pressed & (ps5pad::kL1 | ps5pad::kR1))
				{
					const int count = (int)ps5keyboard::Pages(ps5lang::Current(), true).size();
					m_page = (m_page + ((pressed & ps5pad::kR1) ? 1 : count - 1)) % count;
					m_refocus = m_focusRow >= 0;
				}
				if (pressed & ps5pad::kTriangle)
					m_shift = !m_shift;
				if (chosen < 0 && (pressed & ps5pad::kOptions))
					chosen = (int)buttons.size() - 1;
			}
			return chosen;
		}

	private:
		bool m_shift = false; // capitals and the second symbols
		int m_page = 0;		  // which of the language's pages
		// the focused letter's place (row -1: another key's), where the focus stays when L1 or R1 change
		// the page, or as near as the page's keys go: refocus, for the frame after
		int m_focusRow = -1, m_focusColumn = 0;
		bool m_refocus = false;

		// what was typed becomes the text when the game's length allows it
		void Type(const std::string& text, const KeyboardAsk& ask)
		{
			if (ask.maxLength < 0 || (int)ps5keyboard::Utf16Length(text) <= ask.maxLength)
				typed = text;
		}

		// The end of text, as much of it as fits a width, after an ellipsis: what was typed last, where
		// the cursor is. Arabic's, drawn right to left, as it is.
		static std::string Tail(const Canvas& canvas, ImFont* font, const std::string& text, float width)
		{
			const float whole = canvas.Width(font, 24, text);
			const std::u32string all = ui::Decode(text);
			if (whole <= width || ui::HasRightToLeft(all))
				return text;
			// about as many characters as fit, by their average width, then one at a time
			auto fits = [&](size_t n) { return canvas.Width(font, 24, ui::Encode(U"…" + all.substr(all.size() - n))) <= width; };
			size_t keep = std::min(all.size(), (size_t)(all.size() * width / whole));
			while (keep > 1 && !fits(keep))
				keep--;
			while (keep < all.size() && fits(keep + 1))
				keep++;
			std::u32string tail = all.substr(all.size() - keep);
			while (!tail.empty() && ui::IsCombiningMark(tail.front()))
				tail.erase(0, 1); // never a mark without its letter
			return ui::Encode(U"…" + tail);
		}
	};
}
