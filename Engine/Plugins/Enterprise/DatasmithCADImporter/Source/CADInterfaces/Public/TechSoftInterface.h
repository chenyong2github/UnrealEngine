// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADData.h"

#ifdef USE_TECHSOFT_SDK
#include "A3DSDKErrorCodes.h"
#endif

class A3DImport;
typedef void A3DAsmModelFile;

namespace CADLibrary
{
	CADINTERFACES_API void InitializeTechSoftInterface();
	CADINTERFACES_API bool TECHSOFT_InitializeKernel(const TCHAR* = TEXT(""));

	CADINTERFACES_API class ITechSoftInterface
	{
	public:

		virtual ~ITechSoftInterface() = default;

		/*
		* Returns true if the object has been created outside of the memory pool of the running process
		* This is the case when the object has been created by the DatasmithRuntime plugin
		*/
		virtual bool IsExternal() = 0;
		virtual void SetExternal(bool Value) = 0;

		virtual bool InitializeKernel(const TCHAR* = TEXT("")) = 0;

#ifdef USE_TECHSOFT_SDK
		virtual A3DStatus Import(const A3DImport& Import) = 0;
		virtual A3DAsmModelFile* GetModelFile() = 0;
#endif
	};

	CADINTERFACES_API TSharedPtr<ITechSoftInterface>& GetTechSoftInterface();
}

