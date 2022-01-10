// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#endif

THIRD_PARTY_INCLUDES_START
#ifdef USE_TECHSOFT_SDK
#include "A3DSDKIncludes.h"
#endif
THIRD_PARTY_INCLUDES_END

#if PLATFORM_WINDOWS
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace CADLibrary
{
CADINTERFACES_API bool TECHSOFT_InitializeKernel(const TCHAR* = TEXT(""));

CADINTERFACES_API class FTechSoftInterface
{
public:
	/*
	* Returns true if the object has been created outside of the memory pool of the running process
	* This is the case when the object has been created by the DatasmithRuntime plugin
	*/
	bool IsExternal()
	{
		return bIsExternal;
	}

	void SetExternal(bool Value)
	{
		bIsExternal = Value;
	}

	bool InitializeKernel(const TCHAR* = TEXT(""));


#ifdef USE_TECHSOFT_SDK
	A3DStatus Import(const A3DImport& Import);
	A3DAsmModelFile* GetModelFile();
#endif

private:

	bool bIsExternal = false;
	bool bIsInitialize = false;

#ifdef USE_TECHSOFT_SDK
	TUniquePtr<A3DSDKHOOPSExchangeLoader> ExchangeLoader;
#endif
};

CADINTERFACES_API FTechSoftInterface& GetTechSoftInterface();

}

