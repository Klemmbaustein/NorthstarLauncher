#pragma once

#include <windows.h>
#include <string>

extern wchar_t exePath[4096];
extern wchar_t buffer1[8192];
extern wchar_t buffer2[12288];

enum class NorthstarLoadType
{
	vanillaPlus,
	northstar,
	none,
};

void LibraryLoadError(DWORD dwMessageId, const std::wstring& libName, const wchar_t* location);
NorthstarLoadType GetNorthstarLoadType();
bool ProvisionNorthstar();
