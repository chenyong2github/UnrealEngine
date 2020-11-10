// Copyright Epic Games, Inc. All Rights Reserved.

#include "MPCDIModule.h"

#include "MPCDICommon.h"
#include "MPCDIData.h"
#include "MPCDILog.h"
#include "MPCDIRegion.h"
#include "MPCDIShader.h"
#include "MPCDIStrings.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

#include "Misc/DisplayClusterHelpers.h"
#include "Misc/FileHelper.h"

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
bool FMPCDIModule::Load(const FString& LocalMPCDIFile)
{
	FScopeLock lock(&DataGuard);

	if (IsLoaded(LocalMPCDIFile))
	{
		return true;
	}

	TSharedPtr<FMPCDIData> DataItem = MakeShared<FMPCDIData>();
	if (!DataItem->LoadFromFile(LocalMPCDIFile))
	{
		//! Handle error
		return false;
	}

	// Load and cache the file
	MPCDIData.Add(DataItem);

	return true;
}

bool FMPCDIModule::IsLoaded(const FString& LocalMPCDIFile)
{
	FScopeLock lock(&DataGuard);
	return MPCDIData.ContainsByPredicate([LocalMPCDIFile](const TSharedPtr<FMPCDIData>& DataItem)
	{
		return FPaths::IsSamePath(LocalMPCDIFile, DataItem->GetLocalMPCIDIFile());
	});
}

bool FMPCDIModule::GetRegionLocator(const FString& LocalMPCDIFile, const FString& BufferName, const FString& RegionName, IMPCDI::FRegionLocator& OutRegionLocator)
{
	FScopeLock lock(&DataGuard);
	
	// Reset to defaults
	OutRegionLocator = IMPCDI::FRegionLocator();
	
	FMPCDIData* DataItemPtr = nullptr;

	// Find the file index
	for (int FileIndex = 0; FileIndex < MPCDIData.Num(); ++FileIndex)
	{
		if (FPaths::IsSamePath(MPCDIData[FileIndex]->GetLocalMPCIDIFile(), LocalMPCDIFile))
		{
			DataItemPtr = MPCDIData[FileIndex].Get();
			OutRegionLocator.FileIndex = FileIndex;
			break;
		}
	}

	return DataItemPtr && DataItemPtr->FindRegion(BufferName, RegionName, OutRegionLocator);
}

bool FMPCDIModule::SetStaticMeshWarp(const IMPCDI::FRegionLocator& InRegionLocator, UStaticMeshComponent* MeshComponent, USceneComponent* OriginComponent)
{
	FScopeLock lock(&DataGuard);

	if (InRegionLocator.RegionIndex >= 0)
	{
		if (MPCDIData.Num() > InRegionLocator.FileIndex && MPCDIData[InRegionLocator.FileIndex].IsValid())
		{
			FMPCDIRegion* DstRegion = MPCDIData[InRegionLocator.FileIndex]->GetRegion(InRegionLocator);
			if (DstRegion)
			{
				return DstRegion->SetStaticMeshWarp(MeshComponent, OriginComponent);
			}
		}
	}

	return false;
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
	//Support runtime PFM reload
	ReloadAll_RenderThread();

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


//@ Support ext PFM & PNG files
bool FMPCDIModule::CreateCustomRegion(const FString& LocalMPCDIFile, const FString& BufferName, const FString& RegionName, IMPCDI::FRegionLocator& OutRegionLocator)
{
	FScopeLock lock(&DataGuard);
	if (GetRegionLocator(LocalMPCDIFile, BufferName, RegionName, OutRegionLocator))
	{
		return true;
	}

	if (OutRegionLocator.FileIndex < 0)
	{
		//Create new file:
		TSharedPtr<FMPCDIData> DataItem = MakeShared<FMPCDIData>();
		DataItem->Initialize(LocalMPCDIFile, EMPCDIProfileType::mpcdi_A3D); // Use a3d as default profile
		// Load and cache the file
		MPCDIData.Add(DataItem);
		OutRegionLocator.FileIndex = MPCDIData.Num() - 1; // Update locator
	}

	return MPCDIData[OutRegionLocator.FileIndex]->AddRegion(BufferName, RegionName, OutRegionLocator);
}

bool FMPCDIModule::SetMPCDIProfileType(const IMPCDI::FRegionLocator& InRegionLocator, const EMPCDIProfileType ProfileType)
{
	FScopeLock lock(&DataGuard);

	if (InRegionLocator.FileIndex >= 0)
	{
		FMPCDIData& Dst = *MPCDIData[InRegionLocator.FileIndex];
		FString LocalMPCIDIFile = Dst.GetLocalMPCIDIFile();
		Dst.Initialize(LocalMPCIDIFile, ProfileType);

		return true;
	}

	return false;
}

bool FMPCDIModule::LoadPFM(const IMPCDI::FRegionLocator& InRegionLocator, const FString& LocalPFMFile, const float PFMFileScale, bool bIsUnrealGameSpace)
{
	FScopeLock lock(&DataGuard);

	if (InRegionLocator.RegionIndex >= 0)
	{
		if (MPCDIData.Num() > InRegionLocator.FileIndex)
		{
			FMPCDIData& Dst = *MPCDIData[InRegionLocator.FileIndex];
			FMPCDIRegion* DstRegion = Dst.GetRegion(InRegionLocator);
			if (DstRegion)
			{
				return DstRegion->LoadExtPFMFile(LocalPFMFile, Dst.GetProfileType(), PFMFileScale, bIsUnrealGameSpace);
			}
		}
	}

	return false;
}

bool FMPCDIModule::LoadPFMGeometry(const IMPCDI::FRegionLocator& InRegionLocator, const TArray<FVector>& PFMPoints, int DimW, int DimH, const float PFMScale, bool bIsUnrealGameSpace)
{
	FScopeLock lock(&DataGuard);

	if (InRegionLocator.RegionIndex >= 0)
	{
		if (MPCDIData.Num() > InRegionLocator.FileIndex)
		{
			FMPCDIData& Dst = *MPCDIData[InRegionLocator.FileIndex];
			FMPCDIRegion* DstRegion = Dst.GetRegion(InRegionLocator);
			if (DstRegion)
			{
				return DstRegion->LoadExtGeometry(PFMPoints, DimW, DimH, Dst.GetProfileType(), PFMScale, bIsUnrealGameSpace);
			}
		}
	}

	return false;
}

bool FMPCDIModule::LoadAlphaMap(const IMPCDI::FRegionLocator& InRegionLocator, const FString& LocalPNGFile, float GammaValue)
{
	FScopeLock lock(&DataGuard);

	if (InRegionLocator.RegionIndex >= 0)
	{
		if (MPCDIData.Num() > InRegionLocator.FileIndex)
		{
			FMPCDIData& Dst = *MPCDIData[InRegionLocator.FileIndex];
			FMPCDIRegion* DstRegion = Dst.GetRegion(InRegionLocator);
			if (DstRegion)
			{
				return DstRegion->LoadExtAlphaMap(LocalPNGFile,GammaValue);
			}
		}
	}

	return false;
}

bool FMPCDIModule::LoadBetaMap(const IMPCDI::FRegionLocator& InRegionLocator, const FString& LocalPNGFile)
{
	FScopeLock lock(&DataGuard);

	if (InRegionLocator.RegionIndex >= 0)
	{
		if (MPCDIData.Num() > InRegionLocator.FileIndex)
		{
			FMPCDIData& Dst = *MPCDIData[InRegionLocator.FileIndex];
			FMPCDIRegion* DstRegion = Dst.GetRegion(InRegionLocator);
			if (DstRegion)
			{
				return DstRegion->LoadExtBetaMap(LocalPNGFile);
			}
		}
	}

	return false;
}

bool FMPCDIModule::Load(const ConfigParser& CfgData, IMPCDI::FRegionLocator& OutRegionLocator)
{
	bool bResult = false;
	if (CfgData.IsExtConfig())
	{
		if (CreateCustomRegion(CfgData.MPCDIFileName, CfgData.BufferId, CfgData.RegionId, OutRegionLocator))
		{
			SetMPCDIProfileType(OutRegionLocator, CfgData.MPCDIType);
			bResult = true;
		}
	}
	else
	{
		// Check if MPCDI file exists
		if (!FPaths::FileExists(CfgData.MPCDIFileName))
		{
			UE_LOG(LogMPCDI, Warning, TEXT("File not found: %s"), *CfgData.MPCDIFileName);
			return false;
		}

		// Load MPCDI file
		if (!Load(CfgData.MPCDIFileName))
		{
			UE_LOG(LogMPCDI, Warning, TEXT("Couldn't load MPCDI file: %s"), *CfgData.MPCDIFileName);
			return false;
		}

		// Store MPCDI region locator for this viewport
		if (!GetRegionLocator(CfgData.MPCDIFileName, CfgData.BufferId, CfgData.RegionId, OutRegionLocator))
		{
			UE_LOG(LogMPCDI, Warning, TEXT("Couldn't get region locator for <buf %s, reg %s> in file: %s"), *CfgData.BufferId, *CfgData.RegionId, *CfgData.MPCDIFileName);
			return false;
		}

		bResult = true;
	}

	{
		// PFM
		if (!CfgData.PFMFile.IsEmpty())
		{
			if (!LoadPFM(OutRegionLocator, CfgData.PFMFile, CfgData.PFMFileScale, CfgData.bIsUnrealGameSpace))
			{
				UE_LOG(LogMPCDI, Error, TEXT("Failed to load PFM <buf %s, reg %s> from file: %s"), *CfgData.BufferId, *CfgData.RegionId, *CfgData.PFMFile);
			}
		}
		// AlphaMAP
		if (!CfgData.AlphaFile.IsEmpty())
		{
			if (!LoadAlphaMap(OutRegionLocator, CfgData.AlphaFile, CfgData.AlphaGamma))
			{
				UE_LOG(LogMPCDI, Warning, TEXT("Couldn't get load ext alphamap for <buf %s, reg %s> from file: %s"), *CfgData.BufferId, *CfgData.RegionId, *CfgData.AlphaFile);
			}
		}
		// BetaMAP
		if (!CfgData.BetaFile.IsEmpty())
		{
			if (!LoadBetaMap(OutRegionLocator, CfgData.BetaFile))
			{
				UE_LOG(LogMPCDI, Warning, TEXT("Couldn't get load ext betamap for <buf %s, reg %s> from file: %s"), *CfgData.BufferId, *CfgData.RegionId, *CfgData.BetaFile);
			}
		}
	}

	return bResult;
}

void FMPCDIModule::ReloadAll()
{
	for (auto& It : MPCDIData)
	{
		It->ReloadAll();
	}
}

void FMPCDIModule::ReloadAll_RenderThread()
{
	FScopeLock lock(&DataGuard);

	check(IsInRenderingThread());

	for (auto& It : MPCDIData)
	{
		It->ReloadChangedExternalFiles_RenderThread();
	}
}

bool FMPCDIModule::LoadConfig(const TMap<FString, FString>& InConfigParameters, ConfigParser& OutConfig)
{
	OutConfig.ConfigParameters = InConfigParameters;

	// PFM file (optional)
	FString LocalPFMFile;
	if (DisplayClusterHelpers::map::template ExtractValue(InConfigParameters, DisplayClusterMPCDIStrings::cfg::FilePFM, LocalPFMFile))
	{
		UE_LOG(LogMPCDI, Log, TEXT("Found Argument '%s'='%s'"), DisplayClusterMPCDIStrings::cfg::FilePFM, *LocalPFMFile);
		OutConfig.PFMFile = LocalPFMFile;
	}

	// Buffer
	if (!DisplayClusterHelpers::map::template ExtractValue(InConfigParameters, DisplayClusterMPCDIStrings::cfg::Buffer, OutConfig.BufferId))
	{
		if (OutConfig.PFMFile.IsEmpty())
		{
			UE_LOG(LogMPCDI, Error, TEXT("Argument '%s' not found in the config file"), DisplayClusterMPCDIStrings::cfg::Buffer);
			return false;
		}
	}

	// Region
	if (!DisplayClusterHelpers::map::template ExtractValue(InConfigParameters, DisplayClusterMPCDIStrings::cfg::Region, OutConfig.RegionId))
	{
		if (OutConfig.PFMFile.IsEmpty())
		{
			UE_LOG(LogMPCDI, Error, TEXT("Argument '%s' not found in the config file"), DisplayClusterMPCDIStrings::cfg::Region);
			return false;
		}
	}

	// Filename
	FString LocalMPCDIFileName;
	if (DisplayClusterHelpers::map::template ExtractValue(InConfigParameters, DisplayClusterMPCDIStrings::cfg::File, LocalMPCDIFileName))
	{
		UE_LOG(LogMPCDI, Log, TEXT("Found mpcdi file name for %s:%s - %s"), *OutConfig.BufferId, *OutConfig.RegionId, *LocalMPCDIFileName);
		OutConfig.MPCDIFileName = LocalMPCDIFileName;
	}

	// Do Autofill for ext config:
	if (!OutConfig.PFMFile.IsEmpty())
	{
		if (OutConfig.RegionId.IsEmpty())
		{
			OutConfig.RegionId = OutConfig.PFMFile;
		}

		if (OutConfig.BufferId.IsEmpty())
		{
			OutConfig.BufferId = DisplayClusterMPCDIStrings::cfg::PFMFileDefaultID;
		}

		if (OutConfig.MPCDIFileName.IsEmpty())
		{
			OutConfig.MPCDIFileName = DisplayClusterMPCDIStrings::cfg::PFMFileDefaultID;
		}
	}

	// Origin node (optional)
	if (DisplayClusterHelpers::map::template ExtractValue(InConfigParameters, DisplayClusterMPCDIStrings::cfg::Origin, OutConfig.OriginType))
	{
		UE_LOG(LogMPCDI, Log, TEXT("Found origin node for %s:%s - %s"), *OutConfig.BufferId, *OutConfig.RegionId, *OutConfig.OriginType);
	}
	else
	{
		UE_LOG(LogMPCDI, Log, TEXT("No origin node found for %s:%s. VR root will be used as default."), *OutConfig.BufferId, *OutConfig.RegionId);
	}

	{
		// MPCDIType (optional)
		FString MPCDITypeStr;
		if (!DisplayClusterHelpers::map::template ExtractValue(InConfigParameters, DisplayClusterMPCDIStrings::cfg::MPCDIType, MPCDITypeStr))
		{
			OutConfig.MPCDIType = IMPCDI::EMPCDIProfileType::mpcdi_A3D;
		}
		else
		{
			OutConfig.MPCDIType = IMPCDI::EMPCDIProfileType::Invalid;

			static const TArray<FString> Profiles({"2d","3d","a3d","sl"});
			for (int i = 0; i < Profiles.Num(); ++i)
			{
				if (!MPCDITypeStr.Compare(Profiles[i], ESearchCase::IgnoreCase))
				{
					OutConfig.MPCDIType = (EMPCDIProfileType)i;
					break;
				}
			}

			if (OutConfig.MPCDIType == IMPCDI::EMPCDIProfileType::Invalid)
			{
				UE_LOG(LogMPCDI, Error, TEXT("Argument '%s' has unknown value '%s'"), DisplayClusterMPCDIStrings::cfg::MPCDIType, *MPCDITypeStr);
				return false;
			}
		}
		
		// Default is UE scale, cm
		OutConfig.PFMFileScale = 1;
		if (DisplayClusterHelpers::map::template ExtractValueFromString(InConfigParameters, DisplayClusterMPCDIStrings::cfg::WorldScale, OutConfig.PFMFileScale))
		{
			UE_LOG(LogMPCDI, Log, TEXT("Found WorldScale value for %s:%s - %.f"), *OutConfig.BufferId, *OutConfig.RegionId, OutConfig.PFMFileScale);
		}

		OutConfig.bIsUnrealGameSpace = false;
		if (DisplayClusterHelpers::map::template ExtractValueFromString(InConfigParameters, DisplayClusterMPCDIStrings::cfg::UseUnrealAxis, OutConfig.bIsUnrealGameSpace))
		{
			UE_LOG(LogMPCDI, Log, TEXT("Found bIsUnrealGameSpace value for %s:%s - %s"), *OutConfig.BufferId, *OutConfig.RegionId, OutConfig.bIsUnrealGameSpace?"true":"false");
		}

		// AlphaFile file (optional)
		FString LocalAlphaFile;
		if (DisplayClusterHelpers::map::template ExtractValueFromString(InConfigParameters, DisplayClusterMPCDIStrings::cfg::FileAlpha, LocalAlphaFile))
		{
			UE_LOG(LogMPCDI, Log, TEXT("Found external AlphaMap file for %s:%s - %s"), *OutConfig.BufferId, *OutConfig.RegionId, *LocalAlphaFile);
			OutConfig.AlphaFile = LocalAlphaFile;
		}

		OutConfig.AlphaGamma = 1;
		if (DisplayClusterHelpers::map::template ExtractValueFromString(InConfigParameters, DisplayClusterMPCDIStrings::cfg::AlphaGamma, OutConfig.AlphaGamma))
		{
			UE_LOG(LogMPCDI, Log, TEXT("Found AlphaGamma value for %s:%s - %.f"), *OutConfig.BufferId, *OutConfig.RegionId, OutConfig.AlphaGamma);
		}

		// BetaFile file (optional)
		FString LocalBetaFile;
		if (DisplayClusterHelpers::map::template ExtractValueFromString(InConfigParameters, DisplayClusterMPCDIStrings::cfg::FileBeta, LocalBetaFile))
		{
			UE_LOG(LogMPCDI, Log, TEXT("Found external BetaMap file for %s:%s - %s"), *OutConfig.BufferId, *OutConfig.RegionId, *LocalBetaFile);
			OutConfig.BetaFile = LocalBetaFile;
		}
	}

	return true;
}

bool FMPCDIModule::GetMPCDIMeshData(const FString& MPCDIFile, const FString& BufferName, const FString& RegionName, FMPCDIGeometryExportData& MeshData)
{
	IMPCDI::FRegionLocator RegionLocator;
	const bool bResult = GetRegionLocator(MPCDIFile, BufferName, RegionName, RegionLocator);
	if (bResult)
	{
		IMPCDI::FShaderInputData ShaderInputData;
		ShaderInputData.RegionLocator = RegionLocator;
		
		TSharedPtr<FMPCDIData> MpcdiData = GetMPCDIData(ShaderInputData);
		if (MpcdiData.IsValid())
		{
			if (FMPCDIRegion* Region = MpcdiData.Get()->GetRegion(RegionLocator))
			{
				return ExportMeshData(Region, MeshData);
			}
		}
	}

	return false;
}

IMPLEMENT_MODULE(FMPCDIModule, MPCDI);
