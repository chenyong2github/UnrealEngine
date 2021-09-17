// Copyright Epic Games, Inc. All Rights Reserved.
#ifdef USE_TECHSOFT_SDK

#define INITIALIZE_A3D_API

#include "TechSoftInterfaceImpl.h"

#include "CADInterfacesModule.h"
#include "Misc/Paths.h"


namespace CADLibrary
{
	bool FTechSoftInterfaceImpl::InitializeKernel(const TCHAR* InEnginePluginsPath)
	{
		if (bIsInitialize)
		{
			return true;
		}

		FString EnginePluginsPath(InEnginePluginsPath);
		if (EnginePluginsPath.IsEmpty())
		{
			EnginePluginsPath = FPaths::EnginePluginsDir();
		}

		FString TechSoftDllPath = FPaths::Combine(EnginePluginsPath, TEXT("Enterprise/DatasmithCADImporter"), TEXT("Binaries"), FPlatformProcess::GetBinariesSubdirectory(), "TechSoft");
		TechSoftDllPath = FPaths::ConvertRelativePathToFull(TechSoftDllPath);
		ExchangeLoader = MakeUnique<A3DSDKHOOPSExchangeLoader>(*TechSoftDllPath);

		const A3DStatus IRet = ExchangeLoader->m_eSDKStatus;
		if (IRet != A3D_SUCCESS)
		{
			UE_LOG(CADInterfaces, Warning, TEXT("Failed to load required library in %s. Plug-in will not be functional."), *TechSoftDllPath);
		}
		else
		{
			bIsInitialize = true;
		}
		return bIsInitialize;
	}

	A3DStatus FTechSoftInterfaceImpl::Import(const A3DImport& Import)
	{
		return ExchangeLoader->Import(Import);
	}

	A3DAsmModelFile* FTechSoftInterfaceImpl::GetModelFile()
	{
		return ExchangeLoader->m_psModelFile;
	}

}
#endif // USE_TECHSOFT_SDK
