// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MPCDIModule.h"

#include "MPCDIData.h"
#include "MPCDIShader.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

#include "RHICommandList.h"


//////////////////////////////////////////////////////////////////////////////////////////////
// IModuleInterface
//////////////////////////////////////////////////////////////////////////////////////////////
void FMPCDIModule::StartupModule()
{
	FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("nDisplay"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/nDisplay"), PluginShaderDir);
}

void FMPCDIModule::ShutdownModule()
{
	ReleaseMPCDIData();
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IMPCDI
//////////////////////////////////////////////////////////////////////////////////////////////
bool FMPCDIModule::Load(const FString& MPCDIFile)
{
	FScopeLock lock(&DataGuard);

	if (IsLoaded(MPCDIFile))
	{
		return true;
	}

	if (!FPaths::FileExists(MPCDIFile))
	{
		return false;
	}

	TSharedPtr<FMPCDIData> DataItem(new FMPCDIData);
	if (!DataItem->Load(MPCDIFile))
	{
		return false;
	}

	// Load and cache the file
	MPCDIData.Add(DataItem);

	return true;
}

bool FMPCDIModule::IsLoaded(const FString& MPCDIFile)
{
	FScopeLock lock(&DataGuard);

	return MPCDIData.ContainsByPredicate([MPCDIFile](const TSharedPtr<FMPCDIData>& DataItem)
	{
		return FPaths::IsSamePath(MPCDIFile, DataItem->GetFilePath());
	});
}

bool FMPCDIModule::GetRegionLocator(const FString& MPCDIFile, const FString& BufferName, const FString& RegionName, IMPCDI::FRegionLocator& OutRegionLocator)
{
	FScopeLock lock(&DataGuard);

	// Reset to defaults
	OutRegionLocator = IMPCDI::FRegionLocator();

	if (!IsLoaded(MPCDIFile))
	{
		if (!Load(MPCDIFile))
		{
			//@todo logs
			return false;
		}
	}

	TSharedPtr<FMPCDIData> DataItem;
	IMPCDI::FRegionLocator TmpRegionLocator;

	// Find the file index
	for (int FileIndex = 0; FileIndex < MPCDIData.Num(); ++FileIndex)
	{
		if (FPaths::IsSamePath(MPCDIData[FileIndex]->GetFilePath(), MPCDIFile))
		{
			DataItem = MPCDIData[FileIndex];
			TmpRegionLocator.FileIndex = FileIndex;
			break;
		}
	}

	// Try to find the requested region
	if (!DataItem->FindRegion(BufferName, RegionName, TmpRegionLocator))
	{
		//@todo logs
		return false;
	}

	// Export data
	OutRegionLocator = TmpRegionLocator;

	return true;
}

bool FMPCDIModule::ComputeFrustum(const IMPCDI::FRegionLocator& RegionLocator, float WorldScale, float ZNear, float ZFar, IMPCDI::FFrustum &InOutFrustum)
{
	FScopeLock lock(&DataGuard);

	if (MPCDIData.Num() > RegionLocator.FileIndex)
	{
		if (MPCDIData[RegionLocator.FileIndex])
		{
			return MPCDIData[RegionLocator.FileIndex]->ComputeFrustum(RegionLocator, InOutFrustum, WorldScale, ZNear, ZFar);
		}
	}

	//@todo log

	return false;
}

TSharedPtr<FMPCDIData> FMPCDIModule::GetMPCDIData(IMPCDI::FShaderInputData& ShaderInputData)
{
	FScopeLock lock(&DataGuard);

	if (MPCDIData.Num() > ShaderInputData.RegionLocator.FileIndex)
	{
		return MPCDIData[ShaderInputData.RegionLocator.FileIndex];
	}
	else
	{
		//@todo: handle error
	}

	return nullptr;
}

bool FMPCDIModule::ApplyWarpBlend(FRHICommandListImmediate& RHICmdList, IMPCDI::FTextureWarpData& TextureWarpData, IMPCDI::FShaderInputData& ShaderInputData)
{
	FScopeLock lock(&DataGuard);

	if (MPCDIData.Num() > ShaderInputData.RegionLocator.FileIndex)
	{
		return FMPCDIShader::ApplyWarpBlend(RHICmdList, TextureWarpData, ShaderInputData, MPCDIData[ShaderInputData.RegionLocator.FileIndex].Get());
	}
	else
	{
		//@todo: handle error
	}

	return false;
}

void FMPCDIModule::ReleaseMPCDIData()
{
	FScopeLock lock(&DataGuard);

	MPCDIData.Empty();
}

IMPLEMENT_MODULE(FMPCDIModule, MPCDI);

