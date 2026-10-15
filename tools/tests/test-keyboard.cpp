// SPDX-License-Identifier: GPL-3.0-or-later
// The on-screen keyboards' keys (port/app/keyboard.cpp) on the PC: every language's pages, and what
// the keys that do more than type a character do (kana's marks, Korean's syllables, Vietnamese's
// tones). tools/tests/test-keyboard.sh builds and runs it. Each check prints PASS or FAIL; the exit
// status is the failures.

#include "app/keyboard.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace ps5keyboard;

namespace
{
	int failures = 0;

	void Check(bool ok, const std::string& what)
	{
		std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
		failures += !ok;
	}

	const Page* PageOf(const std::string& language, Kind kind, bool inGame = false)
	{
		for (const Page& page : Pages(language, inGame))
			if (page.kind == kind)
				return &page;
		return nullptr;
	}

	// the key that types text, with Shift or without
	const Key* KeyOf(const Page& page, const std::string& text, bool* shift = nullptr)
	{
		for (bool shifted : {false, true})
			for (const auto& row : page.rows)
				for (const Key& key : row)
					if ((shifted ? key.shifted : key.text) == text)
					{
						if (shift)
							*shift = shifted;
						return &key;
					}
		return nullptr;
	}

	// keys typed one after the other, by what each types
	std::string TypeAll(const Page& page, const std::vector<std::string>& keys)
	{
		std::string text;
		for (const std::string& name : keys)
		{
			bool shift = false;
			const Key* key = KeyOf(page, name, &shift);
			if (!key)
				return "(no key " + name + ")";
			text = Typed(text, page, *key, shift);
		}
		return text;
	}

	void Expect(const std::string& got, const std::string& want, const std::string& what)
	{
		Check(got == want, what + ": " + got + (got == want ? "" : " (want " + want + ")"));
	}
}

int main()
{
	// every language's pages, in the launcher and in game: rows and keys within the keyboards' room,
	// no key without its text, the symbols last
	const char* languages[] = {"ja", "en-US", "en-GB", "fr", "fr-CA", "es", "es-419", "de", "it", "nl", "pt-PT", "pt-BR", "ru", "uk", "pl",
		"cs", "hu", "ro", "el", "tr", "fi", "sv", "da", "nb", "ar", "th", "vi", "id", "ko", "zh-Hans", "zh-Hant", "qps"};
	for (const char* language : languages)
		for (bool inGame : {false, true})
		{
			const std::vector<Page>& pages = Pages(language, inGame);
			bool fits = pages.size() >= 2 && pages.back().label == "#+=";
			std::string labels;
			for (const Page& page : pages)
			{
				labels += " " + page.label;
				fits = fits && !page.rows.empty() && (int)page.rows.size() <= kMaxRows;
				for (const auto& row : page.rows)
				{
					fits = fits && !row.empty() && (int)row.size() <= kMaxKeys;
					for (const Key& key : row)
						fits = fits && !key.text.empty() && !key.shifted.empty();
				}
			}
			Check(fits, std::string(language) + (inGame ? " in game:" : ":") + labels);
		}
	// a language not written in Latin letters opens on its own, but for a name or an address
	Check(FirstPage("ru", false) == 0 && Pages("ru")[FirstPage("ru", true)].label == "abc", "Russian opens on its letters, Latin for an address");
	Check(FirstPage("de", false) == 0 && Pages("de")[0].label == "abc", "German opens on Latin letters");

	// Korean: jamo into syllables, and back out a jamo at a time; none of it in game
	const Page* hangul = PageOf("ko", Kind::Hangul);
	Check(hangul != nullptr, "Korean has its page");
	Check(!PageOf("ko", Kind::Hangul, true), "Korean's syllables are not in game");
	if (hangul)
	{
		Expect(TypeAll(*hangul, {"ㅎ", "ㅏ", "ㄴ", "ㄱ", "ㅡ", "ㄹ"}), "한글", "Hangul");
		Expect(TypeAll(*hangul, {"ㄷ", "ㅏ", "ㄹ", "ㄱ"}), "닭", "a double final");
		Expect(TypeAll(*hangul, {"ㄷ", "ㅏ", "ㄹ", "ㄱ", "ㅏ"}), "달가", "a final moving on");
		Expect(TypeAll(*hangul, {"ㅇ", "ㅗ", "ㅏ"}), "와", "a double vowel");
		Expect(TypeAll(*hangul, {"ㄱ", "ㅏ", "ㅂ", "ㅅ", "ㅣ"}), "갑시", "half a double final moving on");
		Expect(TypeAll(*hangul, {"ㅃ", "ㅏ", "ㄹ", "ㄹ", "ㅣ"}), "빨리", "a tense initial");
		Expect(TypeAll(*hangul, {"ㅅ", "ㅏ", "ㄹ", "ㅏ", "ㅇ", "ㅎ", "ㅐ"}), "사랑해", "a sentence");
		Expect(TypeAll(*hangul, {"ㅏ", "ㄱ"}), "ㅏㄱ", "a vowel alone");
		Expect(TypeAll(*hangul, {"ㅇ", "ㅏ", "ㄸ"}), "아ㄸ", "a consonant that is never final");
		std::string text = "닭";
		std::string steps;
		for (int i = 0; i < 4; i++)
			steps += " " + (text = Deleted(text));
		Expect(steps, " 달 다 ㄷ ", "deleted a jamo at a time");
		Expect(Deleted("와"), "오", "a double vowel deleted");
	}
	Expect(Deleted("aé"), "a", "a letter deleted whole");
	Expect(Deleted(""), "", "nothing to delete");

	// kana: ゛ ゜ and 小 change the kana before them, and change it back
	const Page& hiragana = Pages("ja")[0];
	const Page& katakana = Pages("ja")[1];
	Expect(TypeAll(hiragana, {"か", "゛"}), "が", "゛");
	Expect(TypeAll(hiragana, {"か", "゛", "゛"}), "か", "゛ again");
	Expect(TypeAll(hiragana, {"は", "゛", "゜"}), "ぱ", "゜ on a voiced kana");
	Expect(TypeAll(hiragana, {"つ", "小"}), "っ", "小");
	Expect(TypeAll(hiragana, {"う", "゛"}), "ゔ", "ゔ");
	Expect(TypeAll(katakana, {"カ", "小"}), "ヵ", "ヵ");
	Expect(TypeAll(katakana, {"ウ", "゛"}), "ヴ", "ヴ");
	Expect(TypeAll(katakana, {"ア", "゛"}), "ア", "゛ where it does not fit");
	Expect(TypeAll(hiragana, {"゛"}), "", "゛ alone");

	// Vietnamese: a tone goes on the vowel before it, or off it again; its key shows it on an a
	const Page* vietnamese = PageOf("vi", Kind::Vietnamese);
	Check(vietnamese != nullptr, "Vietnamese has its tones");
	if (vietnamese)
	{
		const Key* grave = KeyOf(*vietnamese, "̀");
		const Key* acute = KeyOf(*vietnamese, "́");
		const Key* hook = KeyOf(*vietnamese, "̉");
		Check(grave && acute && hook, "the tone keys");
		if (grave && acute && hook)
		{
			Expect(Typed("a", *vietnamese, *grave, false), "à", "a tone");
			Expect(Typed("à", *vietnamese, *acute, false), "á", "another tone");
			Expect(Typed("á", *vietnamese, *acute, false), "a", "the same tone off");
			Expect(Typed("ngư", *vietnamese, *hook, false), "ngử", "a tone on a horned vowel");
			Expect(Typed("Ơ", *vietnamese, *grave, false), "Ờ", "a tone on a capital");
			Expect(Typed("b", *vietnamese, *grave, false), "b", "a tone after a consonant");
			Expect(Label(*grave, false) + Label(*hook, false), "àả", "the tone keys' labels");
		}
	}

	// Thai's marks show on a dotted circle; Turkish's dotless and dotted i
	const Page& thai = Pages("th")[0];
	Expect(Label(thai.rows[0][5], false), "◌ุ", "a Thai mark's label");
	Expect(Label(thai.rows[0][3], false), "ภ", "a Thai letter's label");
	const Page& turkish = Pages("tr")[1];
	Check(KeyOf(turkish, "ı") && KeyOf(turkish, "ı")->shifted == "I" && KeyOf(turkish, "i") && KeyOf(turkish, "i")->shifted == "İ", "Turkish's i");

	// lengths, as the launcher counts them and as the 3DS and the Wii U do
	Check(Length("aé한😀") == 4 && Utf16Length("aé한😀") == 5, "lengths in characters and in UTF-16");
	Expect(FromUtf16(std::u16string{u'a', (char16_t)0xd83d, (char16_t)0xde00}), "a😀", "UTF-16 with a surrogate pair");
	Expect(FromUtf16(std::u16string{u'a', (char16_t)0xd83d, u'b'}), "a�b", "UTF-16 with a lone surrogate");
	Check(ToUtf16("aé한😀") == std::u16string({u'a', u'é', u'한', (char16_t)0xd83d, (char16_t)0xde00}), "UTF-8 to UTF-16");

	// the in-game fonts' characters: the kana's marked forms, Vietnamese's toned vowels, no jamo; the
	// dotted circle only where a key shows a mark on it; a Latin language's Latin only, so it needs
	// no second font
	const std::u32string japanese = Characters("ja"), viet = Characters("vi");
	Check(japanese.find(U'が') != std::u32string::npos && japanese.find(U'ヵ') != std::u32string::npos, "Japanese's marked kana in game");
	Check(viet.find(U'ự') != std::u32string::npos && viet.find(U'̀') == std::u32string::npos, "Vietnamese's toned vowels in game, no bare tone");
	Check(Characters("ko").find(U'ㅂ') == std::u32string::npos, "no jamo in game");
	Check(Characters("th").find(U'◌') != std::u32string::npos && Characters("de").find(U'◌') == std::u32string::npos,
		"the dotted circle for Thai, not German");
	bool latin = true;
	for (const char* language : {"en-GB", "de", "fr", "es", "pl", "cs", "tr", "vi", "ro", "hu"})
		for (char32_t c : Characters(language))
			latin = latin && (c < 0x250 || (c >= 0x1e00 && c < 0x1f00) || (c >= 0x2010 && c < 0x2030) || c == 0x20ac);
	Check(latin, "Latin languages' characters are Lexend's");

	std::printf("%s (%d failed)\n", failures ? "FAILED" : "all passed", failures);
	return failures;
}
