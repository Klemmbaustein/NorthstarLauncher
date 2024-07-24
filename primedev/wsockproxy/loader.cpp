#include "loader.h"
#include <shlwapi.h>
#include <string>
#include <system_error>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <iostream>

#include "MinHook.h"

namespace fs = std::filesystem;

static wchar_t northstarPath[8192];
static wchar_t exePath[4096];

bool GetExePathWide(wchar_t* dest, DWORD destSize)
{
	if (!dest)
		return NULL;
	if (destSize < MAX_PATH)
		return NULL;

	DWORD length = GetModuleFileNameW(NULL, dest, destSize);
	return length && PathRemoveFileSpecW(dest);
}


void LibraryLoadError(DWORD dwMessageId, const std::wstring& libName, const wchar_t* location)
{
	char text[4096];
	std::string message = std::system_category().message(dwMessageId);
	sprintf_s(text, "Failed to load the %ls at \"%ls\" (%lu):\n\n%hs", libName.c_str(), location, dwMessageId, message.c_str());
	if (dwMessageId == 126 && std::filesystem::exists(location))
	{
		sprintf_s(
			text,
			"%s\n\nThe file at the specified location DOES exist, so this error indicates that one of its *dependencies* failed to be "
			"found.",
			text);
	}
	MessageBoxA(GetForegroundWindow(), text, "Northstar Wsock32 Proxy Error", 0);
}

NorthstarLoadType GetNorthstarLoadType()
{
	if (strstr(GetCommandLineA(), "-northstar"))
	{
		return NorthstarLoadType::northstar;
	}
#ifdef NORTHSTAR_IS_VANILLAPLUS
	if (strstr(GetCommandLineA(), "-vanillaPlus"))
	{
		return NorthstarLoadType::vanillaPlus;
	}
#endif
	return NorthstarLoadType::none;
}

static FARPROC LoadNorthstarDLL(std::wstring ProfileName, std::wstring DllName)
{
	if (!GetExePathWide(exePath, 4096))
	{
		MessageBoxA(
			GetForegroundWindow(),
			"Failed getting game directory.\nThe game cannot continue and has to exit.",
			"Northstar Wsock32 Proxy Error",
			0);
		abort();
	}

	// Check if "Northstar.dll" exists in profile directory, if it doesnt fall back to root
	swprintf_s(northstarPath, L"%s\\%s\\%s", exePath, ProfileName.c_str(), DllName.c_str());

	if (!fs::exists(fs::path(northstarPath)))
		swprintf_s(northstarPath, L"%s\\%s", exePath, DllName.c_str());

	std::wcout << L"[*] Using: " << northstarPath << std::endl;

	HMODULE hHookModule = LoadLibraryExW(northstarPath, 0, 8u);
	FARPROC Hook_Init = nullptr;
	if (hHookModule)
		Hook_Init = GetProcAddress(hHookModule, "InitialiseNorthstar");
	if (!hHookModule || !Hook_Init)
	{
		LibraryLoadError(GetLastError(), DllName, northstarPath);
		abort();
	}

	return Hook_Init;
}

static std::wstring ParseCommandLineWstring(std::wstring ArgName, std::wstring Default)
{
	ArgName = L"-" + ArgName + L"=";
	wchar_t* clArgChar = StrStrW(GetCommandLineW(), ArgName.c_str());
	if (!clArgChar)
	{
		return Default;
	}
	std::wstring cla = clArgChar;
	std::wstring dirname;
	if (cla.substr(9, 1) != L"\"")
	{
		size_t space = cla.find(L" ");
		dirname = cla.substr(9, space - 9);
		return dirname;
	}

	std::wstring quote = L"\"";
	size_t quote1 = cla.find(quote);
	size_t quote2 = (cla.substr(quote1 + 1)).find(quote);
	dirname = cla.substr(quote1 + 1, quote2);
	return dirname;
}

bool LoadNorthstar(NorthstarLoadType type)
{
	FARPROC Hook_Init = nullptr;
	{
		std::cout << "[*] Loading northstar with mode: ";
		switch (type)
		{
		case NorthstarLoadType::vanillaPlus:
			std::cout << "Vanilla+";
			break;
		case NorthstarLoadType::northstar:
			std::cout << "Northstar";
			break;
		}
		std::cout << std::endl;

		std::wstring defaultProfile = L"R2VanillaPlus";
		std::wstring defaultDll = L"VPNorthstar.dll";

		if (type != NorthstarLoadType::vanillaPlus)
		{
			defaultProfile = L"R2Northstar";
			defaultDll = L"Northstar.dll";
		}

		std::wstring profile = ParseCommandLineWstring(L"profile", defaultProfile);
		std::wstring dll = ParseCommandLineWstring(L"dllName", defaultDll);

		std::wcout << "[*] Profile: " << profile << ", DLL: " << dll << std::endl;

		Hook_Init = LoadNorthstarDLL(profile, dll);
	}

	((bool (*)())Hook_Init)();

	return true;
}

typedef int (*LauncherMainType)(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow);
LauncherMainType LauncherMainOriginal;

int LauncherMainHook(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	NorthstarLoadType type = GetNorthstarLoadType();
	if (type != NorthstarLoadType::none)
		LoadNorthstar(type);
	return LauncherMainOriginal(hInstance, hPrevInstance, lpCmdLine, nCmdShow);
}

bool ProvisionNorthstar()
{
	if (GetNorthstarLoadType() == NorthstarLoadType::none)
		return true;

	if (MH_Initialize() != MH_OK)
	{
		MessageBoxA(
			GetForegroundWindow(), "MH_Initialize failed\nThe game cannot continue and has to exit.", "Northstar Wsock32 Proxy Error", 0);
		return false;
	}

	auto launcherHandle = GetModuleHandleA("launcher.dll");
	if (!launcherHandle)
	{
		MessageBoxA(
			GetForegroundWindow(),
			"Launcher isn't loaded yet.\nThe game cannot continue and has to exit.",
			"Northstar Wsock32 Proxy Error",
			0);
		return false;
	}

	LPVOID pTarget = (LPVOID)GetProcAddress(launcherHandle, "LauncherMain");
	if (MH_CreateHook(pTarget, (LPVOID)&LauncherMainHook, reinterpret_cast<LPVOID*>(&LauncherMainOriginal)) != MH_OK ||
		MH_EnableHook(pTarget) != MH_OK)
		MessageBoxA(GetForegroundWindow(), "Hook creation failed for function LauncherMain.", "Northstar Wsock32 Proxy Error", 0);

	return true;
}
