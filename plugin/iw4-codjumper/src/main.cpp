#include <xtl.h>
#include <cstdint>
#include <string>

#include "detour.h"
#include "structs.h"

extern "C"
{
	long DbgPrint(const char *format, ...);

	uint32_t XamGetCurrentTitleId();

	uint32_t ExCreateThread(
		HANDLE *pHandle,
		uint32_t stackSize,
		uint32_t *pThreadId,
		void *pApiThreadStartup,
		PTHREAD_START_ROUTINE pStartAddress,
		void *pParameter,
		uint32_t creationFlags);
}

void *ResolveFunction(const std::string &moduleName, uint32_t ordinal)
{
	HMODULE moduleHandle = GetModuleHandle(moduleName.c_str());
	if (moduleHandle == nullptr)
		return nullptr;

	return GetProcAddress(moduleHandle, reinterpret_cast<const char *>(ordinal));
}

typedef void (*XNOTIFYQUEUEUI)(uint32_t type, uint32_t userIndex, uint64_t areas, const wchar_t *displayText, void *pContextData);
XNOTIFYQUEUEUI XNotifyQueueUI = static_cast<XNOTIFYQUEUEUI>(ResolveFunction("xam.xex", 656));

namespace game
{
	unsigned int TITLE_ID = 0x41560817;

	const int CONTENTS_PLAYERCLIP = 0x10000;

	// Game functions
	char *(*va)(char *format, ...) = reinterpret_cast<char *(*)(char *format, ...)>(0x823160A8);

	int (*Weapon_RocketLauncher_Fire)(gentity_s *ent, unsigned int weaponIndex, double spread, weaponParms *wp, weaponParms *gunVel, int a6, int a7, int a8) = reinterpret_cast<int (*)(gentity_s *ent, unsigned int weaponIndex, double spread, weaponParms *wp, weaponParms *gunVel, int a6, int a7, int a8)>(0x82260C90);

	void (*CG_GameMessage)(int localClientNum, const char *msg) = reinterpret_cast<void (*)(int localClientNum, const char *msg)>(0x8213DE38);

	typedef void (*Scr_Function)();

	Scr_Function *(*Scr_GetFunction)(const char **pName, int *type) = reinterpret_cast<Scr_Function *(*)(const char **pName, int *type)>(0x82254C38);
	Scr_Function *(*Common_GetFunction)(const char **pName, int *type) = reinterpret_cast<Scr_Function *(*)(const char **pName, int *type)>(0x8224D450);
	Scr_Function *(*Objectives_GetFunction)(const char **pName, int *type) = reinterpret_cast<Scr_Function *(*)(const char **pName, int *type)>(0x82259268);
	Scr_Function *(*BuiltIn_GetFunction)(const char **pName, int *type) = reinterpret_cast<Scr_Function *(*)(const char **pName, int *type)>(0x82254B50);
	char *(*Scr_AddSourceBuffer)(const char *filename, const char *extFilename, const char *codePos, bool archive) = reinterpret_cast<char *(*)(const char *filename, const char *extFilename, const char *codePos, bool archive)>(0x8229F2C8);
	char *(*Scr_ReadFile_FastFile)(const char *filename, const char *extFilename, const char *codePos, bool archive) = reinterpret_cast<char *(*)(const char *filename, const char *extFilename, const char *codePos, bool archive)>(0x8229F250);
	void (*Scr_GetVector)(unsigned int index, float *vectorValue) = reinterpret_cast<void (*)(unsigned int index, float *vectorValue)>(0x822B35B8);

	int (*FS_FOpenFileReadForThread)(const char *filename, _iobuf **file) = reinterpret_cast<int (*)(const char *filename, _iobuf **file)>(0x822F6530);
	unsigned int (*FS_ReadFile)(const char *qpath, void **buffer) = reinterpret_cast<unsigned int (*)(const char *qpath, void **buffer)>(0x822F6730);
	void (*FS_FCloseFile)(_iobuf *h) = reinterpret_cast<void (*)(_iobuf *h)>(0x822F63D8);
}

Detour Weapon_RocketLauncher_Fire_Detour;

int Weapon_RocketLauncher_Fire_Hook(gentity_s *ent, unsigned int weaponIndex, double spread, weaponParms *wp, weaponParms *gunVel, int a6, int a7, int a8)
{
	// COD4 logic for RPG knockback
	auto client = ent->client;
	if (client)
	{
		ent->client->ps.velocity[0] = client->ps.velocity[0] - wp->forward[0] * 64.0f;
		ent->client->ps.velocity[1] = client->ps.velocity[1] - wp->forward[1] * 64.0f;
		ent->client->ps.velocity[2] = client->ps.velocity[2] - wp->forward[2] * 64.0f;
	}

	// omits the original function logic

	return 0;
}

unsigned int BRUSH_MAX_HEIGHT = 150;

void RemoveBrushCollisions()
{
	// cm.numBrushes
	unsigned __int16 *cm_numBrushesPtr = reinterpret_cast<unsigned __int16 *>(0x8305270C);
	unsigned __int16 cm_numBrushes = *cm_numBrushesPtr;

	DbgPrint("[PLUGIN] Found %d brushes\n", cm_numBrushes);

	// cm.brushBounds
	Bounds **cm_brushBoundsArrayPtr = reinterpret_cast<Bounds **>(0x83052714);
	Bounds *cm_brushBoundsFirst = *cm_brushBoundsArrayPtr;

	// cm.brushContents
	int **cm_brushContentsArrayPtr = reinterpret_cast<int **>(0x83052718);
	int *cm_brushContentsFirst = *cm_brushContentsArrayPtr;

	for (int i = 0; i < cm_numBrushes; i++)
	{
		float height = 2.0f * cm_brushBoundsFirst[i].halfSize[1];
		if (height > BRUSH_MAX_HEIGHT)
			cm_brushContentsFirst[i] &= ~game::CONTENTS_PLAYERCLIP;
	}

	game::CG_GameMessage(0, "Brush collision removed");
}

Detour Scr_AddSourceBuffer_Detour;

char *Scr_AddSourceBuffer_Hook(const char *filename, const char *extFilename, const char *codePos, bool archive)
{
	// DbgPrint("[PLUGIN][Scr_AddSourceBuffer_Hook] filename=%s extFilename=%s\n", filename, extFilename);

	// Load shadowed files from mod folder
	char *path = game::va("mod/%s", extFilename);
	_iobuf *file = nullptr;
	auto file_size = game::FS_FOpenFileReadForThread(path, &file);

	if (file_size != -1)
	{
		DbgPrint("[PLUGIN][Scr_AddSourceBuffer_Hook] Loading file from mod folder: FilePath=%s\n", path);
		void *fileData = nullptr;
		int result = game::FS_ReadFile(path, &fileData);
		if (result)
			return static_cast<char *>(fileData);

		game::FS_FCloseFile(file);
	}

	return game::Scr_ReadFile_FastFile(extFilename, extFilename, codePos, archive);
}

void ApplyBounceDepatch()
{
	*(volatile uint32_t *)0x8210CB70 = 0x60000000;
	*(volatile uint32_t *)0x8210CB74 = 0x60000000;
}

uint32_t PluginMain()
{
	Sleep(500);
	XNotifyQueueUI(0, 0, XNOTIFY_SYSTEM, L"IW4 CodJumper Loaded", nullptr);

	ApplyBounceDepatch();

	Scr_AddSourceBuffer_Detour = Detour(game::Scr_AddSourceBuffer, Scr_AddSourceBuffer_Hook);
	Scr_AddSourceBuffer_Detour.Install();

	Weapon_RocketLauncher_Fire_Detour = Detour(game::Weapon_RocketLauncher_Fire, Weapon_RocketLauncher_Fire_Hook);
	Weapon_RocketLauncher_Fire_Detour.Install();

	Sleep(30000);
	RemoveBrushCollisions();

	return 0;
}

void MonitorTitleId(void *pThreadParameter)
{
	for (;;)
	{
		if (XamGetCurrentTitleId() == game::TITLE_ID)
		{
			PluginMain();
			break;
		}
		else
			Sleep(100);
	}
}

int DllMain(HANDLE hModule, DWORD reason, void *pReserved)
{
	switch (reason)
	{
	case DLL_PROCESS_ATTACH:

		ExCreateThread(nullptr, 0, nullptr, nullptr, reinterpret_cast<PTHREAD_START_ROUTINE>(MonitorTitleId), nullptr, 2);
		break;
	case DLL_PROCESS_DETACH:
		break;
	}

	return TRUE;
}
