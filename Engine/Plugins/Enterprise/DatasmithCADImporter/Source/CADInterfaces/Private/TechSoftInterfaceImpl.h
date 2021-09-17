// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#ifdef USE_TECHSOFT_SDK
#include "CADData.h"
#include "CADOptions.h"
#include "CADSceneGraph.h"
#include "TechSoftInterface.h"

// undefine UE TEXT to include windows.h
#undef TEXT
#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

// redefine UE TEXT
#undef TEXT
#if !defined(TEXT) && !UE_BUILD_DOCS
#if PLATFORM_TCHAR_IS_CHAR16
#define TEXT_PASTE(x) u ## x
#else
#define TEXT_PASTE(x) L ## x
#endif
#define TEXT(x) TEXT_PASTE(x)
#endif


#pragma warning(push)
#pragma warning(disable:4505)
#pragma warning(disable:4191)
#include "A3DSDKIncludes.h"
#pragma warning(pop)

namespace CADLibrary
{
	class CADINTERFACES_API FTechSoftInterfaceImpl : public ITechSoftInterface
	{
	public:
		FTechSoftInterfaceImpl(bool bSetExternal = false)
			: bIsExternal(bSetExternal)
		{}

		virtual bool InitializeKernel(const TCHAR* EnginePluginsPath) override;

		virtual bool IsExternal() override
		{
			return bIsExternal;
		}

		virtual void SetExternal(bool Value) override
		{
			bIsExternal = Value;
		}

		virtual A3DStatus Import(const A3DImport& Import) override;
		virtual A3DAsmModelFile* GetModelFile() override;

	private:

		bool bIsExternal = false;
		bool bIsInitialize = false;
		TUniquePtr<A3DSDKHOOPSExchangeLoader> ExchangeLoader;

	};
}

#endif // USE_TECHSOFT_SDK


