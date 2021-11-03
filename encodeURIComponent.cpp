#include <iomanip>
#include <unordered_set>
#include "encodeURIComponent.h"

void encodeURIComponent(const char* str, std::ostringstream& encoded)
{
	// 以下の文字以外をエンコード
	// A-Z a-z 0-9 - _ . ! ~ * ' ( )
	static const std::unordered_set<char> ignore_chars = {
		'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
		'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
		'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
		'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
		'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
		'-', '_', '.', '!', '~', '*', '\'', '(', ')'
	};

	while (*str != '\0')
	{
		if (ignore_chars.count(*str) == 0) {
			encoded << '%' << std::setfill('0') << std::right
				   << std::setw(2) << std::uppercase << std::hex
				   // 符号拡張回避のため0xffでマスクする
				   << int(0xff & *str);
		}
		else {
			encoded << *str;
		}

		str++;
	}
}
