#pragma once
#include <sstream>

/**
 * URLエンコードを行う
 * @param[in] str URLエンコードする文字列。UTF-8でエンコードされている必要がある。
 * @param[out] encoded URLエンコードした文字列を格納するostringstream。
 */
void encodeURIComponent(const char* str, std::ostringstream& encoded);
