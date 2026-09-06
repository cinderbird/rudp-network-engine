#pragma once
#include <string>

static std::wstring ConvertToWString(const char* input)
{
	int size_needed = MultiByteToWideChar(CP_UTF8, 0, input, -1, NULL, 0);
	std::wstring result(size_needed, 0);
	MultiByteToWideChar(CP_UTF8, 0, input, -1, &result[0], size_needed);
	return result;
}

static std::wstring ConvertToWString(const std::string& str)
{
	int32_t size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
	std::wstring wstr(size_needed, 0);
	MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size_needed);
	if (!wstr.empty() && wstr.back() == L'\0') {
		wstr.pop_back();
	}
	return wstr;
}

static std::string ConvertToString(const std::wstring& wstr)
{
	if (wstr.empty()) return {};

	int32_t size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
	std::string str(size_needed, 0);
	WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], size_needed, nullptr, nullptr);

	if (!str.empty() && str.back() == '\0') {
		str.pop_back();
	}

	return str;
}
