/*
 * Copyright (c) 2008-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */


#ifdef WIN32
#include "ModuleUpdateLoader.h"

typedef HMODULE (GetUpdatedModule_FUNC)(const char*, const char*);

ModuleUpdateLoader::ModuleUpdateLoader(const char* updateLoaderDllName)
	: mGetUpdatedModuleFunc(NULL)
{
// @ATG_CHANGE : BEGIN HoloLens support
#if PX_HOLOLENS
	WCHAR updateLoaderDllNameW[MAX_PATH];
	if (0 != MultiByteToWideChar(CP_ACP, 0, updateLoaderDllName, -1, updateLoaderDllNameW, MAX_PATH))
	{
		mUpdateLoaderDllHandle = LoadPackagedLibrary(updateLoaderDllNameW, 0);
	}
#else
	mUpdateLoaderDllHandle = LoadLibrary(updateLoaderDllName);
#endif
// @ATG_CHANGE : END

	if (mUpdateLoaderDllHandle != NULL)
	{
		mGetUpdatedModuleFunc = GetProcAddress(mUpdateLoaderDllHandle, "GetUpdatedModule");
	}
}

ModuleUpdateLoader::~ModuleUpdateLoader()
{
	if (mUpdateLoaderDllHandle != NULL)
	{
		FreeLibrary(mUpdateLoaderDllHandle);
		mUpdateLoaderDllHandle = NULL;
	}
}

HMODULE ModuleUpdateLoader::loadModule(const char* moduleName, const char* appGuid)
{
	HMODULE result = NULL;

	if (mGetUpdatedModuleFunc != NULL)
	{
		// Try to get the module through PhysXUpdateLoader
		GetUpdatedModule_FUNC* getUpdatedModuleFunc = (GetUpdatedModule_FUNC*)mGetUpdatedModuleFunc;
		result = getUpdatedModuleFunc(moduleName, appGuid);
	}
	else
	{
		// If no PhysXUpdateLoader, just load the DLL directly
// @ATG_CHANGE : BEGIN HoloLens support
#if PX_HOLOLENS
		WCHAR updateLoaderDllNameW[MAX_PATH];
		if (0 != MultiByteToWideChar(CP_ACP, 0, moduleName, -1, updateLoaderDllNameW, MAX_PATH))
		{
			mUpdateLoaderDllHandle = LoadPackagedLibrary(updateLoaderDllNameW, 0);
		}
		result = LoadPackagedLibrary(updateLoaderDllNameW, 0);
#else
		result = LoadLibrary(moduleName);
#endif
// @ATG_CHANGE : END
	}

	return result;
}


#endif	// WIN32
