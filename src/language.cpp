#include "language.h"

Language gLanguage = Language::Italian;

const char* Tr(const char* italian, const char* english)
{
    return gLanguage == Language::English ? english : italian;
}

std::wstring TrW(const wchar_t* italian, const wchar_t* english)
{
    return gLanguage == Language::English ? english : italian;
}

std::wstring CountItems(std::size_t count)
{
    return std::to_wstring(count) + L" " +
           TrW(count == 1 ? L"elemento" : L"elementi", count == 1 ? L"item" : L"items");
}

std::wstring VerbItems(std::size_t count, const wchar_t* oneIt, const wchar_t* manyIt,
                       const wchar_t* en)
{
    return TrW(count == 1 ? oneIt : manyIt, en) + CountItems(count);
}
