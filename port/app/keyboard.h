// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: the on-screen keyboards' keys, in the menus' language. The launcher's keyboard
// (frontend/shell), the 3DS's in-game one (app/ingame3ds.cpp) and the Wii U's (Cemu's swkbd.cpp,
// patches/cemu) show the same pages, which L1 and R1 go through: the Latin letters and digits, the
// language's own letters (its accented ones, or its alphabet), then the symbols. A key types its
// text, or with Shift its capital. Some pages type more than characters: a kana's ゛, ゜ and 小 change
// the kana before them, Korean's jamo join into syllables, and Vietnamese's tone keys go on the vowel
// before them. Chinese has no page of its own: its characters need an input method with a
// dictionary, which the app does not have.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ps5keyboard
{
	struct Key
	{
		std::string text;	 // what it types (UTF-8)
		std::string shifted; // with Shift
	};

	enum class Kind
	{
		Plain,
		Kana,		// ゛ ゜ and 小 change the kana before them
		Hangul,		// jamo join into syllables
		Vietnamese, // the tone keys go on the vowel before them
	};

	struct Page
	{
		std::string label; // what L1 and R1 call it: "abc", "äöü", "абв", "#+="
		Kind kind = Kind::Plain;
		std::vector<std::vector<Key>> rows; // 2 to kMaxRows rows of up to kMaxKeys
	};

	constexpr int kMaxKeys = 13;
	constexpr int kMaxRows = 5;

	// A language's pages (a code of ps5lang's). inGame leaves out what the in-game keyboards' fonts
	// cannot draw: Korean's syllables, more than eleven thousand of them.
	const std::vector<Page>& Pages(const std::string& language, bool inGame = false);
	// The page a keyboard opens on: a language not written in Latin letters opens on its own, unless
	// latin (a user name, a password, an address)
	int FirstPage(const std::string& language, bool latin, bool inGame = false);
	// The most rows one of the language's pages has: the keyboard's height
	int MostRows(const std::string& language, bool inGame = false);
	// The most keys a row of the language's pages has: the keys' width
	int MostKeys(const std::string& language, bool inGame = false);

	// What a key shows: what it types, a mark that goes on the letter before it on a dotted circle
	std::string Label(const Key& key, bool shift);
	// text with the key typed at its end, as its page types it: the same text when the key changes
	// nothing (a ゛ after a kana it does not fit)
	std::string Typed(const std::string& text, const Page& page, const Key& key, bool shift);
	// text without its last character; a Korean syllable loses its last jamo
	std::string Deleted(const std::string& text);
	// how long text is in characters (code points), and in UTF-16 units, as the 3DS and the Wii U count
	size_t Length(const std::string& text);
	size_t Utf16Length(const std::string& text);
	// UTF-16 (the Wii U's text) as UTF-8, a lone surrogate as U+FFFD, and back
	std::string FromUtf16(std::u16string_view text);
	std::u16string ToUtf16(const std::string& text);
	// every character the language's in-game pages show or type: the in-game fonts'
	std::u32string Characters(const std::string& language);
}
