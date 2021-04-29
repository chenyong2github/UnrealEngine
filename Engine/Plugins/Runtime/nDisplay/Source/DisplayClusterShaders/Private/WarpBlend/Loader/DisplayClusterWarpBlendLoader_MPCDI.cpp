// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterWarpBlendLoader_MPCDI.h"

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

#include "DisplayClusterShadersLog.h"

#include "Misc/DisplayClusterHelpers.h"
#include "Misc/FileHelper.h"

#include "Stats/Stats.h"
#include "Engine/Engine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

#include "WarpBlend/Loader/DisplayClusterWarpBlendLoader_Texture.h"

#include "WarpBlend/DisplayClusterWarpBlend.h"
#include "WarpBlend/DisplayClusterWarpBlend_GeometryContext.h"
#include "WarpBlend/DisplayClusterWarpBlend_GeometryProxy.h"

struct FWarpRegion
{
	EDisplayClusterWarpProfileType ProfileType;

	FMatrix RegionMatrix = FMatrix::Identity;

	float AlphaMapGammaEmbedded = 1.f;

	IDisplayClusterRenderTexture* WarpMap = nullptr;
	IDisplayClusterRenderTexture* AlphaMap = nullptr;
	IDisplayClusterRenderTexture* BetaMap = nullptr;

	bool CreateWarpBlendInterface(TSharedPtr<IDisplayClusterWarpBlend>& OutWarpBlend)
	{
		TSharedPtr<FDisplayClusterWarpBlend> WarpBlend = MakeShared<FDisplayClusterWarpBlend>();

		WarpBlend->GeometryContext.GeometryProxy.GeometryType = EDisplayClusterWarpGeometryType::WarpMap;
		WarpBlend->GeometryContext.ProfileType = ProfileType;
		WarpBlend->GeometryContext.RegionMatrix = RegionMatrix;

		FDisplayClusterWarpBlend_GeometryProxy& Proxy = WarpBlend->GeometryContext.GeometryProxy;
		Proxy.AlphaMap = AlphaMap;
		Proxy.AlphaMapEmbeddedGamma = AlphaMapGammaEmbedded;

		Proxy.BetaMap = BetaMap;
		Proxy.WarpMap = WarpMap;

		OutWarpBlend = WarpBlend;
		
		return true;
	}


	void ReleaseResources()
	{
		if (WarpMap)
		{
			delete WarpMap;
			WarpMap = nullptr;
		}
		if (AlphaMap)
		{
			delete AlphaMap;
			AlphaMap = nullptr;
		}
		if (BetaMap)
		{
			delete BetaMap;
			BetaMap = nullptr;
		}
	}
};

EDisplayClusterWarpProfileType ImplGetProfileType(mpcdi::Profile* profile)
{
	if (profile)
	{
		switch (profile->GetProfileType())
		{
		case mpcdi::ProfileType2d: return EDisplayClusterWarpProfileType::warp_2D;
		case mpcdi::ProfileType3d: return EDisplayClusterWarpProfileType::warp_3D;
		case mpcdi::ProfileTypea3: return EDisplayClusterWarpProfileType::warp_A3D;
		case mpcdi::ProfileTypesl: return EDisplayClusterWarpProfileType::warp_SL;
		default:
			// Invalid profile type
			break;
		}
	}

	return EDisplayClusterWarpProfileType::Invalid;
};

bool ImplLoadRegion(EDisplayClusterWarpProfileType ProfileType, mpcdi::Region* mpcdiRegion, FWarpRegion& Out)
{
	Out.ProfileType = ProfileType;

	float X = mpcdiRegion->GetX();
	float Y = mpcdiRegion->GetY();
	float W = mpcdiRegion->GetXsize();
	float H = mpcdiRegion->GetYsize();

	// Build Region matrix
	Out.RegionMatrix = FMatrix::Identity;
	Out.RegionMatrix.M[0][0] = W;
	Out.RegionMatrix.M[1][1] = H;
	Out.RegionMatrix.M[3][0] = X;
	Out.RegionMatrix.M[3][1] = Y;
	
	mpcdi::AlphaMap* AlphaMapSource = mpcdiRegion->GetFileSet()->GetAlphaMap();
	if (AlphaMapSource != nullptr)
	{
		Out.AlphaMap = FDisplayClusterWarpBlendLoader_Texture::CreateBlendMap(AlphaMapSource);
		Out.AlphaMapGammaEmbedded = AlphaMapSource->GetGammaEmbedded();
	}

	mpcdi::BetaMap* BetaMapSource = mpcdiRegion->GetFileSet()->GetBetaMap();
	if (BetaMapSource != nullptr)
	{
		Out.BetaMap = FDisplayClusterWarpBlendLoader_Texture::CreateBlendMap(BetaMapSource);
	}


	if (ProfileType != EDisplayClusterWarpProfileType::warp_SL)
	{
		mpcdi::GeometryWarpFile* geometryFile = mpcdiRegion->GetFileSet()->GetGeometryWarpFile();
		if (geometryFile)
		{
			Out.WarpMap = FDisplayClusterWarpBlendLoader_Texture::CreateWarpMap(ProfileType, geometryFile);
			return Out.WarpMap != nullptr;
		}
	}

	return false;
}

bool FDisplayClusterWarpBlendLoader_MPCDI::Load(const FDisplayClusterWarpBlendConstruct::FLoadMPCDIFile& InParameters, TSharedPtr<IDisplayClusterWarpBlend>& OutWarpBlend)
{
	FString MPCIDIFileFullPath = DisplayClusterHelpers::filesystem::GetFullPathForConfigResource(InParameters.MPCDIFileName);

	if (!FPaths::FileExists(MPCIDIFileFullPath))
	{
		UE_LOG(LogDisplayClusterWarpBlend, Error, TEXT("File not found: %s"), *MPCIDIFileFullPath);
		return false;
	}

	mpcdi::Profile* profile = mpcdi::Profile::CreateProfile();
	mpcdi::Reader* reader = mpcdi::Reader::CreateReader();
	std::string version = reader->GetSupportedVersions();

	EDisplayClusterWarpProfileType ProfileType = ImplGetProfileType(profile);

	mpcdi::MPCDI_Error mpcdi_err = reader->Read(TCHAR_TO_ANSI(*MPCIDIFileFullPath), profile);
	delete reader;

	UE_LOG(LogDisplayClusterWarpBlend, Log, TEXT("Loading MPCDI file %s."), *MPCIDIFileFullPath);

	if (MPCDI_FAILED(mpcdi_err))
	{
		UE_LOG(LogDisplayClusterWarpBlend, Error, TEXT("Error %d reading MPCDI file"), int32(mpcdi_err));
		return false;
	}

	FString Version = version.c_str();
	UE_LOG(LogDisplayClusterWarpBlend, Verbose, TEXT("Version: %s"), *Version);

	// Find desired region:
	for (mpcdi::Display::BufferIterator itBuffer = profile->GetDisplay()->GetBufferBegin(); itBuffer != profile->GetDisplay()->GetBufferEnd(); ++itBuffer)
	{
		mpcdi::Buffer* mpcdiBuffer = itBuffer->second;

		FString BufferId(mpcdiBuffer->GetId().c_str());
		if (InParameters.BufferId.Equals(BufferId, ESearchCase::IgnoreCase))
		{
			for (mpcdi::Buffer::RegionIterator it = mpcdiBuffer->GetRegionBegin(); it != mpcdiBuffer->GetRegionEnd(); ++it)
			{
				mpcdi::Region* mpcdiRegion = it->second;
				FString RegionId(mpcdiRegion->GetId().c_str());

				if (InParameters.RegionId.Equals(RegionId, ESearchCase::IgnoreCase))
				{
					FWarpRegion RegionResources;
					if (!ImplLoadRegion(ProfileType, mpcdiRegion, RegionResources))
					{
						UE_LOG(LogDisplayClusterWarpBlend, Error, TEXT("Can't load region '%s' buffer '%s' from mpcdi file '%s'"), *InParameters.RegionId, *InParameters.BufferId, *InParameters.MPCDIFileName);
						RegionResources.ReleaseResources();
						return false;
					}

					//ok, Create and initialize warpblend interface
					return RegionResources.CreateWarpBlendInterface(OutWarpBlend);
				}
			}
		}
	}

	UE_LOG(LogDisplayClusterWarpBlend, Error, TEXT("Can't find region '%s' buffer '%s' inside mpcdi file '%s'"), *InParameters.RegionId, *InParameters.BufferId, *InParameters.MPCDIFileName);
	return false;
}

bool FDisplayClusterWarpBlendLoader_MPCDI::Load(const FDisplayClusterWarpBlendConstruct::FLoadPFMFile& InParameters, TSharedPtr<IDisplayClusterWarpBlend>& OutWarpBlend)
{
	FWarpRegion RegionResources;
	RegionResources.ProfileType = InParameters.ProfileType;
	RegionResources.AlphaMapGammaEmbedded = InParameters.AlphaMapEmbeddedAlpha;

	RegionResources.WarpMap = FDisplayClusterWarpBlendLoader_Texture::CreateWarpMap(RegionResources.ProfileType, InParameters.PFMFileName, InParameters.PFMScale, InParameters.bIsUnrealGameSpace);
	if (RegionResources.WarpMap)
	{
		if (InParameters.AlphaMapFileName.IsEmpty() == false)
		{
			RegionResources.AlphaMap = FDisplayClusterWarpBlendLoader_Texture::CreateBlendMap(InParameters.AlphaMapFileName);
		}

		if (InParameters.BetaMapFileName.IsEmpty() == false)
		{
			RegionResources.BetaMap = FDisplayClusterWarpBlendLoader_Texture::CreateBlendMap(InParameters.BetaMapFileName);
		}

		return RegionResources.CreateWarpBlendInterface(OutWarpBlend);
	}

	RegionResources.ReleaseResources();

	return false;
}



