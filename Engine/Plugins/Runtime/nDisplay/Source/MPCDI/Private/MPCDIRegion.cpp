// Copyright Epic Games, Inc. All Rights Reserved.

#include "MPCDIRegion.h"
#include "MPCDIBlendTexture.h"
#include "MPCDIHelpers.h"

THIRD_PARTY_INCLUDES_START

#include "mpcdiProfile.h"
#include "mpcdiReader.h"
#include "mpcdiDisplay.h"
#include "mpcdiBuffer.h"
#include "mpcdiRegion.h"
#include "mpcdiAlphaMap.h"
#include "mpcdiBetaMap.h"
#include "mpcdiDistortionMap.h"
#include "mpcdiGeometryWarpFile.h"


#include "IO/mpcdiPfmIO.h"
#include "mpcdiPNGReadWrite.h"

THIRD_PARTY_INCLUDES_END

#include "Misc/FileHelper.h"

static TAutoConsoleVariable<int32> CVarMPCDIReloadChangedExtFiles(
	TEXT("nDisplay.reload.pfm"),
	(int)0, // Disabled by default
	TEXT("Changed external files reload period for PFM:\n")
	TEXT("0 : disable\n"),
	ECVF_RenderThreadSafe
);


namespace MPCDI
{
	void FMPCDIRegion::FExternalFileReloader::Initialize(const FString& FullPathFileName)
	{
		bIsEnabled = false;
		FileName = FullPathFileName;
		FrameIndex = 0;
	}
	void FMPCDIRegion::FExternalFileReloader::SyncDateTime()
	{
		DateTime = IFileManager::Get().GetAccessTimeStamp(*FileName);
		bIsEnabled = true;
		FrameIndex = 0;
	}
	void FMPCDIRegion::FExternalFileReloader::Release()
	{
		bIsEnabled = false;
		FrameIndex = 0;
	}
	bool FMPCDIRegion::FExternalFileReloader::IsChanged(bool bForceTest)
	{
		if (bIsEnabled)
		{
			if (!bForceTest)
			{
				// Detect auto reload by frames
				const int FramePeriod = CVarMPCDIReloadChangedExtFiles.GetValueOnAnyThread();
				if (FramePeriod > 0)
				{
					if (FrameIndex++ > FramePeriod)
					{
						FrameIndex = 0;
						bForceTest = true;
					}
				}
			}
			if (bForceTest)
			{
				// Check for file data is modified				
				FDateTime CurrentDateTime = IFileManager::Get().GetAccessTimeStamp(*FileName);
				if (CurrentDateTime != DateTime)
				{
					//! Add log. Reload file *FullPathFileName
					return true;
				}
			}
		}
		return false;
	}

	bool FMPCDIRegion::Load(mpcdi::Region* mpcdiRegion, const IMPCDI::EMPCDIProfileType ProfileType)
	{
		ID = FString(mpcdiRegion->GetId().c_str());

		X = mpcdiRegion->GetX();
		Y = mpcdiRegion->GetY();
		W = mpcdiRegion->GetXsize();
		H = mpcdiRegion->GetYsize();
		ResX = mpcdiRegion->GetXresolution();
		ResY = mpcdiRegion->GetYresolution();

		mpcdi::Frustum* mpcdi_frustum = mpcdiRegion->GetFrustum();


		mpcdi::AlphaMap *alphaMap = mpcdiRegion->GetFileSet()->GetAlphaMap();
		if (alphaMap)
		{
			float AlphaEmbeddedGamma = alphaMap->GetGammaEmbedded();
			AlphaMap.LoadBlendMap(alphaMap, AlphaEmbeddedGamma);
		}

		mpcdi::BetaMap *betaMap = mpcdiRegion->GetFileSet()->GetBetaMap();
		if (betaMap)
		{
			BetaMap.LoadBlendMap(betaMap,1);
		}

		if (ProfileType != IMPCDI::EMPCDIProfileType::mpcdi_SL)
		{
			mpcdi::GeometryWarpFile *geometryFile = mpcdiRegion->GetFileSet()->GetGeometryWarpFile();
			if (geometryFile)
			{
				WarpMap.LoadWarpMap(geometryFile, ProfileType);
			}
		}

		return true;
	}

	bool FMPCDIRegion::LoadExtGeometry(const TArray<FVector>& PFMPoints, int DimW, int DimH, const IMPCDI::EMPCDIProfileType ProfileType, const float WorldScale, bool bIsUnrealGameSpace)
	{
		return WarpMap.LoadCustom3DWarpMap(PFMPoints, DimW, DimH, ProfileType, WorldScale, bIsUnrealGameSpace);
	}

	bool FMPCDIRegion::LoadExtPFMFile(const FString& LocalPFMFileName, const IMPCDI::EMPCDIProfileType ProfileType, const float PFMScale, bool bIsUnrealGameSpace)
	{
		FString PFMFileName = DisplayClusterHelpers::config::GetFullPath(LocalPFMFileName);
		if (!FPaths::FileExists(PFMFileName))
		{
			//! Handle error: pfm not found
			return false;
		}

		// Initialize runtime reloader data:
		ExtPFMFile.File.Initialize(PFMFileName);
		ExtPFMFile.ProfileType = ProfileType;
		ExtPFMFile.PFMScale = PFMScale;
		ExtPFMFile.bIsUnrealGameSpace = bIsUnrealGameSpace;

		return LoadExtPFMFile(ExtPFMFile);
	}

	bool FMPCDIRegion::LoadExtPFMFile(FExtPFMFile& PFMFile)
	{
		bool bResult = false;
		std::string FileName = TCHAR_TO_ANSI(*PFMFile.File());
		mpcdi::PFM* PFMData;
		mpcdi::MPCDI_Error res = mpcdi::PfmIO::Read(FileName, PFMData);
		if (mpcdi::MPCDI_SUCCESS == res && PFMData)
		{
			if (WarpMap.LoadPFMFile(*PFMData, PFMFile.ProfileType, PFMFile.PFMScale, PFMFile.bIsUnrealGameSpace))
			{				
				PFMFile.File.SyncDateTime();
				bResult = true;
			}
			delete PFMData;
		}
		else
		{
			//@todo: Handle error
		}
		return bResult;
	}

	bool FMPCDIRegion::LoadDataMap(const FString& LocalPNGFileName, float GammaValue, EDataMapType DataType)
	{
		FString PNGFileName = DisplayClusterHelpers::config::GetFullPath(LocalPNGFileName);
		if (!FPaths::FileExists(PNGFileName))
		{
			//! Handle error: blend map file not found
			return false;
		}

		FExtBlendMapFile* ExtBlendMapFile = nullptr;
		switch (DataType)
		{
		case EDataMapType::Alpha:
			ExtBlendMapFile = &ExtAlphaMap;
			break;
		case EDataMapType::Beta:
			ExtBlendMapFile = &ExtBetaMap;
			GammaValue = 1;
			break;
		}

		if (ExtBlendMapFile != nullptr)
		{
			ExtBlendMapFile->File.Initialize(PNGFileName);
			ExtBlendMapFile->GammaValue = GammaValue;
			ExtBlendMapFile->DataType = DataType;
			return LoadDataMap(*ExtBlendMapFile);
		}

		//! handle error
		return false;
	}

	bool FMPCDIRegion::LoadDataMap(FExtBlendMapFile& BlendMapFile)
	{
		bool bResult = false;
		std::string FileName = TCHAR_TO_ANSI(*BlendMapFile.File());

		mpcdi::DataMap* PngData;
		mpcdi::MPCDI_Error res = mpcdi::PNGReadWrite::Read(FileName, PngData);
		if (mpcdi::MPCDI_SUCCESS == res && PngData)
		{
			switch (BlendMapFile.DataType)
			{
			case EDataMapType::Alpha:
				AlphaMap.LoadBlendMap(PngData, BlendMapFile.GammaValue);
				bResult = true;
				break;
			case EDataMapType::Beta:
				BetaMap.LoadBlendMap(PngData, BlendMapFile.GammaValue);
				bResult = true;
				break;
			}
			delete PngData;
		}
		else
		{
			//@todo: Handle error
		}

		if (bResult)
		{
			BlendMapFile.File.SyncDateTime();
		}

		return bResult;
	}

	void FMPCDIRegion::ReloadExternalFiles_RenderThread(bool bForceReload)
	{
		check(IsInRenderingThread());

		// Refresh all modified external files
		//Warp mesh texture
		if (ExtPFMFile.File.IsChanged(bForceReload))
		{
			LoadExtPFMFile(ExtPFMFile);
		}
		// Blend maps
		if (ExtAlphaMap.File.IsChanged(bForceReload))
		{
			LoadDataMap(ExtAlphaMap);
		}
		if (ExtBetaMap.File.IsChanged(bForceReload))
		{
			LoadDataMap(ExtBetaMap);
		}
	}
}
