// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: the on-screen keyboards' keys (keyboard.h).

#include "keyboard.h"

#include <algorithm>
#include <initializer_list>
#include <map>
#include <mutex>
#include <string_view>
#ifdef PS5KEYBOARD_CHECK
#include <stdexcept>
#endif

namespace ps5keyboard
{
	namespace
	{
		// -- UTF-8 ----------------------------------------------------------------------------------

		std::u32string Decode(std::string_view text)
		{
			std::u32string out;
			for (size_t i = 0; i < text.size();)
			{
				const unsigned char lead = (unsigned char)text[i++];
				int extra = lead >= 0xf0 ? 3 : lead >= 0xe0 ? 2 : lead >= 0xc0 ? 1 : 0;
				char32_t code = extra ? lead & (0x3f >> extra) : lead;
				for (; extra > 0 && i < text.size(); extra--, i++)
					code = (code << 6) | ((unsigned char)text[i] & 0x3f);
				out.push_back(code);
			}
			return out;
		}

		std::string Encode(char32_t c)
		{
			std::string out;
			if (c < 0x80)
				out += (char)c;
			else if (c < 0x800)
			{
				out += (char)(0xc0 | (c >> 6));
				out += (char)(0x80 | (c & 0x3f));
			}
			else if (c < 0x10000)
			{
				out += (char)(0xe0 | (c >> 12));
				out += (char)(0x80 | ((c >> 6) & 0x3f));
				out += (char)(0x80 | (c & 0x3f));
			}
			else
			{
				out += (char)(0xf0 | (c >> 18));
				out += (char)(0x80 | ((c >> 12) & 0x3f));
				out += (char)(0x80 | ((c >> 6) & 0x3f));
				out += (char)(0x80 | (c & 0x3f));
			}
			return out;
		}

		// where text's last character starts
		size_t LastStart(const std::string& text)
		{
			size_t at = text.size();
			while (at > 0)
				if (((unsigned char)text[--at] & 0xc0) != 0x80)
					break;
			return at;
		}

		char32_t Last(const std::string& text)
		{
			const std::u32string last = Decode(std::string_view(text).substr(LastStart(text)));
			return last.empty() ? 0 : last[0];
		}

		std::string WithLast(const std::string& text, const std::string& last)
		{
			return text.substr(0, LastStart(text)) + last;
		}

		// a mark drawn on the letter before it: Latin's (Vietnamese's tones), Thai's vowels and tones,
		// Arabic's vowel marks
		bool Combining(char32_t c)
		{
			return (c >= 0x0300 && c <= 0x036f) || c == 0x0e31 || (c >= 0x0e34 && c <= 0x0e3a) || (c >= 0x0e47 && c <= 0x0e4e) ||
				(c >= 0x064b && c <= 0x065f) || c == 0x0670;
		}

		// -- the pages -------------------------------------------------------------------------------

		// a row's keys, a space between each, and with Shift (nullptr: the same keys)
		struct Row
		{
			const char* keys;
			const char* shifted;
		};

		std::vector<std::string> Split(const char* keys)
		{
			std::vector<std::string> out;
			const std::string_view all(keys);
			for (size_t at = 0; at < all.size();)
			{
				const size_t space = std::min(all.find(' ', at), all.size());
				if (space > at)
					out.emplace_back(all.substr(at, space - at));
				at = space + 1;
			}
			return out;
		}

		Page Make(const char* label, Kind kind, std::initializer_list<Row> rows)
		{
			Page page{label, kind, {}};
			for (const Row& row : rows)
			{
				const std::vector<std::string> keys = Split(row.keys);
				const std::vector<std::string> shifted = row.shifted ? Split(row.shifted) : keys;
#ifdef PS5KEYBOARD_CHECK
				if (shifted.size() != keys.size())
					throw std::runtime_error(std::string("a row of ") + label + " has " + std::to_string(keys.size()) + " keys and " +
						std::to_string(shifted.size()) + " with Shift");
#endif
				std::vector<Key> out;
				for (size_t i = 0; i < keys.size(); i++)
					out.push_back({keys[i], i < shifted.size() ? shifted[i] : keys[i]});
				page.rows.push_back(std::move(out));
			}
			return page;
		}

		Page Make(const char* label, std::initializer_list<Row> rows)
		{
			return Make(label, Kind::Plain, rows);
		}

		// the keys every keyboard of the app had: digits, letters and some punctuation, with Shift
		// the capitals and the digits' symbols
		Page Latin()
		{
			return Make("abc", {{"1 2 3 4 5 6 7 8 9 0", "! ? # $ % & * ( ) +"},
								   {"q w e r t y u i o p", "Q W E R T Y U I O P"},
								   {"a s d f g h j k l '", "A S D F G H J K L \""},
								   {"z x c v b n m , . -", "Z X C V B N M ; : _"}});
		}

		Page Symbols()
		{
			return Make("#+=", {{"@ # $ % ^ & * ( ) =", nullptr},
								   {"~ ` [ ] { } \\ | / +", nullptr},
								   {"< > ; : ' \" ! ? _ -", nullptr},
								   {"€ £ ¥ ¢ « » ¿ ¡ … °", nullptr}});
		}

		// A language's own page: the letters it adds to the Latin ones, or its alphabet laid out as
		// its keyboards have it (Russian's and Ukrainian's ЙЦУКЕН, Greek's, Arabic's, Thai's Kedmanee,
		// Korean's two-set Hangul), or Japanese's kana in the order of their table
		std::vector<Page> Own(const std::string& base)
		{
			if (base == "cs")
				return {Make("áčř", {{"á č ď é ě í ň ó", "Á Č Ď É Ě Í Ň Ó"}, {"ř š ť ú ů ý ž", "Ř Š Ť Ú Ů Ý Ž"}})};
			if (base == "da" || base == "nb")
				return {Make("æøå", {{"æ ø å é", "Æ Ø Å É"}})};
			if (base == "de")
				return {Make("äöü", {{"ä ö ü ß é", "Ä Ö Ü ẞ É"}})};
			if (base == "es")
				return {Make("ñ", {{"ñ á é í ó ú ü ¿ ¡", "Ñ Á É Í Ó Ú Ü ¿ ¡"}})};
			if (base == "fi")
				return {Make("äöå", {{"ä ö å š ž", "Ä Ö Å Š Ž"}})};
			if (base == "fr")
				return {Make("éàç", {{"é è ê ë à â ä î ï", "É È Ê Ë À Â Ä Î Ï"}, {"ô ö û ù ü ÿ ç œ æ", "Ô Ö Û Ù Ü Ÿ Ç Œ Æ"}})};
			if (base == "hu")
				return {Make("áéő", {{"á é í ó ö ő ú ü ű", "Á É Í Ó Ö Ő Ú Ü Ű"}})};
			if (base == "it")
				return {Make("àèé", {{"à è é ì í ò ó ù ú", "À È É Ì Í Ò Ó Ù Ú"}})};
			if (base == "nl")
				return {Make("éëï", {{"é ë è ê ï ó ö ü á", "É Ë È Ê Ï Ó Ö Ü Á"}})};
			if (base == "pl")
				return {Make("ąęł", {{"ą ć ę ł ń ó ś ź ż", "Ą Ć Ę Ł Ń Ó Ś Ź Ż"}})};
			if (base == "pt")
				return {Make("ãçé", {{"á à â ã é ê í ó ô õ ú ç", "Á À Â Ã É Ê Í Ó Ô Õ Ú Ç"}})};
			if (base == "ro")
				return {Make("ăîș", {{"ă â î ș ț", "Ă Â Î Ș Ț"}})};
			if (base == "sv")
				return {Make("åäö", {{"å ä ö é", "Å Ä Ö É"}})};
			if (base == "tr")
				return {Make("çğş", {{"ç ğ ı i ö ş ü", "Ç Ğ I İ Ö Ş Ü"}})};
			if (base == "vi") // the tones: grave, acute, hook above, tilde, dot below
				return {Make("ăđ", Kind::Vietnamese, {{"ă â đ ê ô ơ ư", "Ă Â Đ Ê Ô Ơ Ư"}, {"̀ ́ ̉ ̃ ̣", nullptr}})};
			if (base == "ru")
				return {Make("абв", {{"ё й ц у к е н г ш щ з х ъ", "Ё Й Ц У К Е Н Г Ш Щ З Х Ъ"},
										{"ф ы в а п р о л д ж э", "Ф Ы В А П Р О Л Д Ж Э"},
										{"я ч с м и т ь б ю", "Я Ч С М И Т Ь Б Ю"}})};
			if (base == "uk")
				return {Make("абв", {{"' й ц у к е н г ш щ з х ї", "' Й Ц У К Е Н Г Ш Щ З Х Ї"},
										{"ф і в а п р о л д ж є", "Ф І В А П Р О Л Д Ж Є"},
										{"ґ я ч с м и т ь б ю", "Ґ Я Ч С М И Т Ь Б Ю"}})};
			if (base == "el")
				return {Make("αβγ", {{"ά έ ή ί ό ύ ώ ϊ ϋ ΐ ΰ", "Ά Έ Ή Ί Ό Ύ Ώ Ϊ Ϋ ΐ ΰ"},
										{"ς ε ρ τ υ θ ι ο π", "Σ Ε Ρ Τ Υ Θ Ι Ο Π"},
										{"α σ δ φ γ η ξ κ λ", "Α Σ Δ Φ Γ Η Ξ Κ Λ"},
										{"ζ χ ψ ω β ν μ", "Ζ Χ Ψ Ω Β Ν Μ"}})};
			if (base == "ar")
				return {Make("أبت", {{"ض ص ث ق ف غ ع ه خ ح ج د", nullptr},
										{"ش س ي ب ل ا ت ن م ك ط", nullptr},
										{"ئ ء ؤ ر لا ى ة و ز ظ ذ", nullptr},
										{"آ أ إ ؛ ، ؟ ـ", nullptr}})};
			if (base == "th")
				return {Make("กขค", {{"ๅ / - ภ ถ ุ ึ ค ต จ ข ช", "+ ๑ ๒ ๓ ๔ ู ฿ ๕ ๖ ๗ ๘ ๙"},
										{"ๆ ไ ำ พ ะ ั ี ร น ย บ ล ฃ", "๐ \" ฎ ฑ ธ ํ ๊ ณ ฯ ญ ฐ , ฅ"},
										{"ฟ ห ก ด เ ้ ่ า ส ว ง", "ฤ ฆ ฏ โ ฌ ็ ๋ ษ ศ ซ ."},
										{"ผ ป แ อ ิ ื ท ม ใ ฝ", "( ) ฉ ฮ ฺ ์ ? ฒ ฬ ฦ"}})};
			if (base == "ja")
				return {Make("あ", Kind::Kana, {{"あ か さ た な は ま や ら わ ゛", nullptr},
												  {"い き し ち に ひ み ゆ り を ゜", nullptr},
												  {"う く す つ ぬ ふ む よ る ん 小", nullptr},
												  {"え け せ て ね へ め 、 れ ー ？", nullptr},
												  {"お こ そ と の ほ も 。 ろ ～ ！", nullptr}}),
					Make("ア", Kind::Kana, {{"ア カ サ タ ナ ハ マ ヤ ラ ワ ゛", nullptr},
											  {"イ キ シ チ ニ ヒ ミ ユ リ ヲ ゜", nullptr},
											  {"ウ ク ス ツ ヌ フ ム ヨ ル ン 小", nullptr},
											  {"エ ケ セ テ ネ ヘ メ 、 レ ー ？", nullptr},
											  {"オ コ ソ ト ノ ホ モ 。 ロ ～ ！", nullptr}})};
			if (base == "ko")
				return {Make("한", Kind::Hangul, {{"ㅂ ㅈ ㄷ ㄱ ㅅ ㅛ ㅕ ㅑ ㅐ ㅔ", "ㅃ ㅉ ㄸ ㄲ ㅆ ㅛ ㅕ ㅑ ㅒ ㅖ"},
													{"ㅁ ㄴ ㅇ ㄹ ㅎ ㅗ ㅓ ㅏ ㅣ", nullptr},
													{"ㅋ ㅌ ㅊ ㅍ ㅠ ㅜ ㅡ", nullptr}})};
			return {};
		}

		// the languages written in other letters than Latin ones: their own page comes first
		bool OwnScript(const std::string& base)
		{
			return base == "ru" || base == "uk" || base == "el" || base == "ar" || base == "th" || base == "ja" || base == "ko";
		}

		// -- kana ------------------------------------------------------------------------------------

		struct KanaForms
		{
			char32_t plain, voiced, semi, small;
		};

		const std::vector<KanaForms>& KanaTable()
		{
			static const std::vector<KanaForms> table = [] {
				// hiragana: the kana ゛ ゜ or 小 fit, with what each makes
				std::vector<KanaForms> forms = {
					{0x3042, 0, 0, 0x3041},		 // あ ぁ
					{0x3044, 0, 0, 0x3043},		 // い ぃ
					{0x3046, 0x3094, 0, 0x3045}, // う ゔ ぅ
					{0x3048, 0, 0, 0x3047},		 // え ぇ
					{0x304a, 0, 0, 0x3049},		 // お ぉ
					{0x304b, 0x304c, 0, 0},		 // か が
					{0x304d, 0x304e, 0, 0},		 // き ぎ
					{0x304f, 0x3050, 0, 0},		 // く ぐ
					{0x3051, 0x3052, 0, 0},		 // け げ
					{0x3053, 0x3054, 0, 0},		 // こ ご
					{0x3055, 0x3056, 0, 0},		 // さ ざ
					{0x3057, 0x3058, 0, 0},		 // し じ
					{0x3059, 0x305a, 0, 0},		 // す ず
					{0x305b, 0x305c, 0, 0},		 // せ ぜ
					{0x305d, 0x305e, 0, 0},		 // そ ぞ
					{0x305f, 0x3060, 0, 0},		 // た だ
					{0x3061, 0x3062, 0, 0},		 // ち ぢ
					{0x3064, 0x3065, 0, 0x3063}, // つ づ っ
					{0x3066, 0x3067, 0, 0},		 // て で
					{0x3068, 0x3069, 0, 0},		 // と ど
					{0x306f, 0x3070, 0x3071, 0}, // は ば ぱ
					{0x3072, 0x3073, 0x3074, 0}, // ひ び ぴ
					{0x3075, 0x3076, 0x3077, 0}, // ふ ぶ ぷ
					{0x3078, 0x3079, 0x307a, 0}, // へ べ ぺ
					{0x307b, 0x307c, 0x307d, 0}, // ほ ぼ ぽ
					{0x3084, 0, 0, 0x3083},		 // や ゃ
					{0x3086, 0, 0, 0x3085},		 // ゆ ゅ
					{0x3088, 0, 0, 0x3087},		 // よ ょ
					{0x308f, 0, 0, 0x308e},		 // わ ゎ
				};
				// katakana, 0x60 on, and the two small ones hiragana has not: ヵ ヶ
				const size_t hiragana = forms.size();
				for (size_t i = 0; i < hiragana; i++)
				{
					const KanaForms& h = forms[i];
					auto on = [](char32_t c) { return c ? c + 0x60 : 0; };
					forms.push_back({on(h.plain), on(h.voiced), on(h.semi), on(h.small)});
				}
				for (KanaForms& k : forms)
					if (k.plain == 0x30ab || k.plain == 0x30b1)
						k.small = k.plain == 0x30ab ? 0x30f5 : 0x30f6;
				return forms;
			}();
			return table;
		}

		// ゛ ゜ or 小 on the kana before it: on, or off when it already has it
		std::string Kana(const std::string& text, char32_t mark)
		{
			const char32_t last = Last(text);
			if (!last)
				return text;
			for (const KanaForms& k : KanaTable())
			{
				if (last != k.plain && last != k.voiced && last != k.semi && last != k.small)
					continue;
				char32_t next = 0;
				if (mark == 0x309b) // ゛
					next = last == k.voiced ? k.plain : last == k.plain || last == k.semi ? k.voiced : 0;
				else if (mark == 0x309c) // ゜
					next = last == k.semi ? k.plain : last == k.plain || last == k.voiced ? k.semi : 0;
				else // 小
					next = last == k.small ? k.plain : last == k.plain ? k.small : 0;
				return next ? WithLast(text, Encode(next)) : text;
			}
			return text;
		}

		// -- Hangul ----------------------------------------------------------------------------------

		// the jamo as the keys type them (Hangul Compatibility Jamo), in the order a syllable's code
		// counts them: its initial consonants, and its finals (0: none); its vowels are 0x314f on
		const char32_t kInitials[19] = {0x3131, 0x3132, 0x3134, 0x3137, 0x3138, 0x3139, 0x3141, 0x3142, 0x3143, 0x3145,
			0x3146, 0x3147, 0x3148, 0x3149, 0x314a, 0x314b, 0x314c, 0x314d, 0x314e};
		const char32_t kFinals[28] = {0, 0x3131, 0x3132, 0x3133, 0x3134, 0x3135, 0x3136, 0x3137, 0x3139, 0x313a, 0x313b, 0x313c,
			0x313d, 0x313e, 0x313f, 0x3140, 0x3141, 0x3142, 0x3144, 0x3145, 0x3146, 0x3147, 0x3148, 0x314a, 0x314b, 0x314c,
			0x314d, 0x314e};

		// a final of two consonants: the final it starts as, and the consonant that joins it
		struct DoubleFinal
		{
			int whole, first;
			char32_t second;
		};
		const DoubleFinal kDoubleFinals[] = {{3, 1, 0x3145}, {5, 4, 0x3148}, {6, 4, 0x314e}, {9, 8, 0x3131}, {10, 8, 0x3141},
			{11, 8, 0x3142}, {12, 8, 0x3145}, {13, 8, 0x314c}, {14, 8, 0x314d}, {15, 8, 0x314e}, {18, 17, 0x3145}};
		// a vowel of two: the two it is made of
		struct DoubleVowel
		{
			int whole, first, second;
		};
		const DoubleVowel kDoubleVowels[] = {{9, 8, 0}, {10, 8, 1}, {11, 8, 20}, {14, 13, 4}, {15, 13, 5}, {16, 13, 20}, {19, 18, 20}};

		bool Syllable(char32_t c)
		{
			return c >= 0xac00 && c <= 0xd7a3;
		}
		char32_t Syllable(int initial, int vowel, int final)
		{
			return 0xac00 + (char32_t)((initial * 21 + vowel) * 28 + final);
		}
		bool Consonant(char32_t c)
		{
			return c >= 0x3131 && c <= 0x314e;
		}
		bool Vowel(char32_t c)
		{
			return c >= 0x314f && c <= 0x3163;
		}
		int Find(const char32_t* list, int count, char32_t c)
		{
			for (int i = 0; i < count; i++)
				if (list[i] == c && c)
					return i;
			return -1;
		}

		// a jamo typed after what is there, as a two-set Korean keyboard joins them
		std::string Hangul(const std::string& text, char32_t jamo)
		{
			const char32_t last = Last(text);
			const bool syllable = Syllable(last);
			const int code = syllable ? (int)(last - 0xac00) : 0;
			const int initial = code / (21 * 28), vowel = code / 28 % 21, final = code % 28;
			if (Consonant(jamo))
			{
				// the final of the syllable before, or a final of two
				if (syllable && !final)
					if (const int f = Find(kFinals, 28, jamo); f > 0)
						return WithLast(text, Encode(Syllable(initial, vowel, f)));
				if (syllable && final)
					for (const DoubleFinal& d : kDoubleFinals)
						if (d.first == final && d.second == jamo)
							return WithLast(text, Encode(Syllable(initial, vowel, d.whole)));
				return text + Encode(jamo);
			}
			const int v = (int)(jamo - 0x314f);
			// a consonant and a vowel: a syllable
			if (Consonant(last))
				if (const int i = Find(kInitials, 19, last); i >= 0)
					return WithLast(text, Encode(Syllable(i, v, 0)));
			// a vowel of two
			if (syllable && !final)
				for (const DoubleVowel& d : kDoubleVowels)
					if (d.first == vowel && d.second == v)
						return WithLast(text, Encode(Syllable(initial, d.whole, 0)));
			// the final before goes on to start a syllable with the vowel (a final of two: its second)
			if (syllable && final)
			{
				int keep = 0;
				char32_t moved = kFinals[final];
				for (const DoubleFinal& d : kDoubleFinals)
					if (d.whole == final)
					{
						keep = d.first;
						moved = d.second;
					}
				if (const int i = Find(kInitials, 19, moved); i >= 0)
					return WithLast(text, Encode(Syllable(initial, vowel, keep)) + Encode(Syllable(i, v, 0)));
			}
			return text + Encode(jamo);
		}

		// -- Vietnamese -------------------------------------------------------------------------------

		// the vowels with each tone: none, grave, acute, hook above, tilde, dot below
		const char* const kVietnamese[] = {"aàáảãạ", "ăằắẳẵặ", "âầấẩẫậ", "eèéẻẽẹ", "êềếểễệ", "iìíỉĩị", "oòóỏõọ", "ôồốổỗộ",
			"ơờớởỡợ", "uùúủũụ", "ưừứửữự", "yỳýỷỹỵ", "AÀÁẢÃẠ", "ĂẰẮẲẴẶ", "ÂẦẤẨẪẬ", "EÈÉẺẼẸ", "ÊỀẾỂỄỆ", "IÌÍỈĨỊ", "OÒÓỎÕỌ",
			"ÔỒỐỔỖỘ", "ƠỜỚỞỠỢ", "UÙÚỦŨỤ", "ƯỪỨỬỮỰ", "YỲÝỶỸỴ"};

		int ToneOf(char32_t mark)
		{
			switch (mark)
			{
			case 0x0300: return 1;
			case 0x0301: return 2;
			case 0x0309: return 3;
			case 0x0303: return 4;
			case 0x0323: return 5;
			default: return 0;
			}
		}

		// a tone on the vowel before it, or off when it already has that one
		std::string Tone(const std::string& text, int tone)
		{
			const char32_t last = Last(text);
			if (!last)
				return text;
			for (const char* vowel : kVietnamese)
			{
				const std::u32string forms = Decode(vowel);
				for (size_t i = 0; i < forms.size(); i++)
					if (forms[i] == last)
						return WithLast(text, Encode(forms[(int)i == tone ? 0 : tone]));
			}
			return text;
		}
	}

	const std::vector<Page>& Pages(const std::string& language, bool inGame)
	{
		static std::mutex mutex;
		static std::map<std::string, std::vector<Page>> made; // its elements stay where they are
		std::lock_guard lock(mutex);
		const std::string key = language + (inGame ? "/in-game" : "");
		auto it = made.find(key);
		if (it == made.end())
		{
			const std::string base = language.substr(0, language.find('-'));
			std::vector<Page> own = Own(base);
			if (inGame)
				own.erase(std::remove_if(own.begin(), own.end(), [](const Page& page) { return page.kind == Kind::Hangul; }), own.end());
			std::vector<Page> pages;
			if (OwnScript(base) && !own.empty())
			{
				pages = std::move(own);
				pages.push_back(Latin());
			}
			else
			{
				pages.push_back(Latin());
				pages.insert(pages.end(), own.begin(), own.end());
			}
			pages.push_back(Symbols());
			it = made.emplace(key, std::move(pages)).first;
		}
		return it->second;
	}

	int FirstPage(const std::string& language, bool latin, bool inGame)
	{
		if (!latin)
			return 0;
		const std::vector<Page>& pages = Pages(language, inGame);
		for (size_t i = 0; i < pages.size(); i++)
			if (pages[i].label == "abc")
				return (int)i;
		return 0;
	}

	int MostRows(const std::string& language, bool inGame)
	{
		size_t most = 0;
		for (const Page& page : Pages(language, inGame))
			most = std::max(most, page.rows.size());
		return (int)most;
	}

	int MostKeys(const std::string& language, bool inGame)
	{
		size_t most = 0;
		for (const Page& page : Pages(language, inGame))
			for (const auto& row : page.rows)
				most = std::max(most, row.size());
		return (int)most;
	}

	std::string Label(const Key& key, bool shift)
	{
		const std::string& text = shift ? key.shifted : key.text;
		const std::u32string code = Decode(text);
		// a Vietnamese tone on an a, as it looks there (the in-game fonts have no dotted circle for a
		// Latin language)
		if (code.size() == 1 && ToneOf(code[0]))
			return Encode(Decode(kVietnamese[0])[ToneOf(code[0])]);
		return !code.empty() && Combining(code[0]) ? "◌" + text : text;
	}

	std::string Typed(const std::string& text, const Page& page, const Key& key, bool shift)
	{
		const std::string& typed = shift ? key.shifted : key.text;
		const std::u32string code = Decode(typed);
		if (code.size() == 1)
		{
			const char32_t c = code[0];
			if (page.kind == Kind::Kana && (c == 0x309b || c == 0x309c || c == 0x5c0f)) // ゛ ゜ 小
				return Kana(text, c);
			if (page.kind == Kind::Hangul && (Consonant(c) || Vowel(c)))
				return Hangul(text, c);
			if (page.kind == Kind::Vietnamese && ToneOf(c))
				return Tone(text, ToneOf(c));
		}
		return text + typed;
	}

	std::string Deleted(const std::string& text)
	{
		const char32_t last = Last(text);
		if (Syllable(last))
		{
			const int code = (int)(last - 0xac00);
			const int initial = code / (21 * 28), vowel = code / 28 % 21, final = code % 28;
			if (final)
			{
				int keep = 0;
				for (const DoubleFinal& d : kDoubleFinals)
					if (d.whole == final)
						keep = d.first;
				return WithLast(text, Encode(Syllable(initial, vowel, keep)));
			}
			for (const DoubleVowel& d : kDoubleVowels)
				if (d.whole == vowel)
					return WithLast(text, Encode(Syllable(initial, d.first, 0)));
			return WithLast(text, Encode(kInitials[initial]));
		}
		return text.substr(0, LastStart(text));
	}

	size_t Length(const std::string& text)
	{
		return Decode(text).size();
	}

	size_t Utf16Length(const std::string& text)
	{
		size_t units = 0;
		for (char32_t c : Decode(text))
			units += c > 0xffff ? 2 : 1;
		return units;
	}

	std::string FromUtf16(std::u16string_view text)
	{
		std::string out;
		for (size_t i = 0; i < text.size(); i++)
		{
			char32_t c = text[i];
			if (c >= 0xd800 && c <= 0xdbff && i + 1 < text.size() && text[i + 1] >= 0xdc00 && text[i + 1] <= 0xdfff)
				c = 0x10000 + ((c - 0xd800) << 10) + (text[++i] - 0xdc00);
			else if (c >= 0xd800 && c <= 0xdfff)
				c = 0xfffd;
			out += Encode(c);
		}
		return out;
	}

	std::u16string ToUtf16(const std::string& text)
	{
		std::u16string out;
		for (char32_t c : Decode(text))
			if (c > 0xffff)
			{
				out.push_back((char16_t)(0xd800 + ((c - 0x10000) >> 10)));
				out.push_back((char16_t)(0xdc00 + ((c - 0x10000) & 0x3ff)));
			}
			else
				out.push_back((char16_t)c);
		return out;
	}

	std::u32string Characters(const std::string& language)
	{
		std::u32string out;
		bool kana = false, vietnamese = false;
		for (const Page& page : Pages(language, true))
		{
			kana |= page.kind == Kind::Kana;
			vietnamese |= page.kind == Kind::Vietnamese;
			for (const auto& row : page.rows)
				for (const Key& key : row)
					for (bool shift : {false, true})
					{
						// what it shows (a mark on its dotted circle), and what it types, but a tone, which
						// changes the vowel before it into one of kVietnamese's
						const std::u32string typed = Decode(shift ? key.shifted : key.text);
						out += Decode(Label(key, shift));
						if (!(typed.size() == 1 && ToneOf(typed[0])))
							out += typed;
					}
		}
		if (kana)
			for (const KanaForms& k : KanaTable())
				for (char32_t c : {k.plain, k.voiced, k.semi, k.small})
					if (c)
						out += c;
		if (vietnamese)
			for (const char* vowel : kVietnamese)
				out += Decode(vowel);
		std::sort(out.begin(), out.end());
		out.erase(std::unique(out.begin(), out.end()), out.end());
		return out;
	}
}
