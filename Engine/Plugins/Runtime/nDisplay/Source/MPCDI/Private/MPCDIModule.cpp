// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MPCDIModule.h"

#include "MPCDIData.h"
#include "MPCDIShader.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

#include "Misc/FileHelper.h"

#include "RHICommandList.h"

#include "MPCDIHelpers.h"
#include "MPCDILog.h"
#include "MPCDIStrings.h"



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

	TSharedPtr<FMPCDIData> DataItem = MakeShareable(new FMPCDIData);
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
	IMPCDI::FRegionLocator TmpRegionLocator;

	// Find the file index
	for (int FileIndex = 0; FileIndex < MPCDIData.Num(); ++FileIndex)
	{
		if (FPaths::IsSamePath(MPCDIData[FileIndex]->GetLocalMPCIDIFile(), LocalMPCDIFile))
		{
			DataItemPtr = MPCDIData[FileIndex].Get();
			TmpRegionLocator.FileIndex = FileIndex;
			break;
		}
	}

	if (!DataItemPtr)
	{
		//! handle error: file not loaded
		return false;
	}

	// Try to find the requested region
	if (!DataItemPtr->FindRegion(BufferName, RegionName, TmpRegionLocator))
	{
		//! Handle error: BufferName + RegionName not defined for LocalMPCDIFile
		return false;
	}

	// Return handler to warp data region
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
		TSharedPtr<FMPCDIData> DataItem = MakeShareable(new FMPCDIData);
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
			MPCDI::FMPCDIRegion* DstRegion = Dst.GetRegion(InRegionLocator);
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
			MPCDI::FMPCDIRegion* DstRegion = Dst.GetRegion(InRegionLocator);
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
			MPCDI::FMPCDIRegion* DstRegion = Dst.GetRegion(InRegionLocator);
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
			MPCDI::FMPCDIRegion* DstRegion = Dst.GetRegion(InRegionLocator);
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


//@todo: Move config parsing and logic to policy mpcdi+picp
// virtual bool LoadConfig(const FString& InConfigLineStr, ConfigParser& OutCfgData) override;
// virtual bool Load(const ConfigParser& CfgData, IMPCDI::FRegionLocator& OutRegionLocator) override;

bool FMPCDIModule::LoadConfig(const FString& ConfigLineStr, ConfigParser& OutConfig)
{
	OutConfig.ConfigLineStr = ConfigLineStr;

	// PFM file (optional)
	FString LocalPFMFile;
	if (DisplayClusterHelpers::str::ExtractValue(ConfigLineStr, DisplayClusterStrings::cfg::data::mpcdi::FilePFM, LocalPFMFile))
	{
		UE_LOG(LogMPCDI, Log, TEXT("Found Argument '%s'='%s'"), DisplayClusterStrings::cfg::data::mpcdi::FilePFM,*LocalPFMFile);
		OutConfig.PFMFile = LocalPFMFile;
	}

	// Buffer
	if (!DisplayClusterHelpers::str::ExtractValue(ConfigLineStr, DisplayClusterStrings::cfg::data::mpcdi::Buffer, OutConfig.BufferId))
	{
		if (OutConfig.PFMFile.IsEmpty())
		{
			UE_LOG(LogMPCDI, Error, TEXT("Argument '%s' not found in the config file"), DisplayClusterStrings::cfg::data::mpcdi::Buffer);
			return false;
		}
	}

	// Region
	if (!DisplayClusterHelpers::str::ExtractValue(ConfigLineStr, DisplayClusterStrings::cfg::data::mpcdi::Region, OutConfig.RegionId))
	{
		if (OutConfig.PFMFile.IsEmpty())
		{
			UE_LOG(LogMPCDI, Error, TEXT("Argument '%s' not found in the config file"), DisplayClusterStrings::cfg::data::mpcdi::Region);
			return false;
		}
	}

	// Filename
	FString LocalMPCDIFileName;
	if (DisplayClusterHelpers::str::ExtractValue(ConfigLineStr, DisplayClusterStrings::cfg::data::mpcdi::File, LocalMPCDIFileName))
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
			OutConfig.BufferId = DisplayClusterStrings::cfg::data::mpcdi::PFMFileDefaultID;
		}

		if (OutConfig.MPCDIFileName.IsEmpty())
		{
			OutConfig.MPCDIFileName = DisplayClusterStrings::cfg::data::mpcdi::PFMFileDefaultID;
		}
	}


	// Origin node (optional)
	if (DisplayClusterHelpers::str::ExtractValue(ConfigLineStr, DisplayClusterStrings::cfg::data::mpcdi::Origin, OutConfig.OriginType))
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
		if (!DisplayClusterHelpers::str::ExtractValue(ConfigLineStr, DisplayClusterStrings::cfg::data::mpcdi::MPCDIType, MPCDITypeStr))
		{
			OutConfig.MPCDIType = IMPCDI::EMPCDIProfileType::mpcdi_A3D;
		}
		else
		{
			OutConfig.MPCDIType = IMPCDI::EMPCDIProfileType::Invalid;

			static const TArray<FString> strEnum({"2d","3d","a3d","sl"});
			for (int i = 0; i < strEnum.Num(); ++i)
			{
				if (!MPCDITypeStr.Compare(strEnum[i], ESearchCase::IgnoreCase))
				{
					OutConfig.MPCDIType = (EMPCDIProfileType)i;
					break;
				}
			}

			if (OutConfig.MPCDIType == IMPCDI::EMPCDIProfileType::Invalid)
			{
				UE_LOG(LogMPCDI, Error, TEXT("Argument '%s' has unknown value '%s'"), DisplayClusterStrings::cfg::data::mpcdi::MPCDIType, *MPCDITypeStr);
				return false;
			}
		}
		
		// Default is UE scale, cm
		OutConfig.PFMFileScale = 1;
		if (DisplayClusterHelpers::str::ExtractValue(ConfigLineStr, DisplayClusterStrings::cfg::data::mpcdi::WorldScale, OutConfig.PFMFileScale))
		{
			UE_LOG(LogMPCDI, Log, TEXT("Found WorldScale value for %s:%s - %.f"), *OutConfig.BufferId, *OutConfig.RegionId, OutConfig.PFMFileScale);
		}
		OutConfig.bIsUnrealGameSpace = false;
		if (DisplayClusterHelpers::str::ExtractValue(ConfigLineStr, DisplayClusterStrings::cfg::data::mpcdi::UseUnrealAxis, OutConfig.bIsUnrealGameSpace))
		{
			UE_LOG(LogMPCDI, Log, TEXT("Found bIsUnrealGameSpace value for %s:%s - %s"), *OutConfig.BufferId, *OutConfig.RegionId, OutConfig.bIsUnrealGameSpace?"true":"false");
		}


		// AlphaFile file (optional)
		FString LocalAlphaFile;
		if (DisplayClusterHelpers::str::ExtractValue(ConfigLineStr, DisplayClusterStrings::cfg::data::mpcdi::FileAlpha, LocalAlphaFile))
		{
			UE_LOG(LogMPCDI, Log, TEXT("Found external AlphaMap file for %s:%s - %s"), *OutConfig.BufferId, *OutConfig.RegionId, *LocalAlphaFile);
			OutConfig.AlphaFile = LocalAlphaFile;
		}
		OutConfig.AlphaGamma = 1;
		if (DisplayClusterHelpers::str::ExtractValue(ConfigLineStr, DisplayClusterStrings::cfg::data::mpcdi::AlphaGamma, OutConfig.AlphaGamma))
		{
			UE_LOG(LogMPCDI, Log, TEXT("Found AlphaGamma value for %s:%s - %.f"), *OutConfig.BufferId, *OutConfig.RegionId, OutConfig.AlphaGamma);
		}

		// BetaFile file (optional)
		FString LocalBetaFile;
		if (DisplayClusterHelpers::str::ExtractValue(ConfigLineStr, DisplayClusterStrings::cfg::data::mpcdi::FileBeta, LocalBetaFile))
		{
			UE_LOG(LogMPCDI, Log, TEXT("Found external BetaMap file for %s:%s - %s"), *OutConfig.BufferId, *OutConfig.RegionId, *LocalBetaFile);
			OutConfig.BetaFile = LocalBetaFile;
		}
		
	}

	return true;
}


IMPLEMENT_MODULE(FMPCDIModule, MPCDI);

