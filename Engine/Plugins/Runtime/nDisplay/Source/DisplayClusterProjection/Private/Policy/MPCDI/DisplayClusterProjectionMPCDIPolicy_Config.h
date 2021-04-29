// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DisplayClusterProjectionLog.h"
#include "DisplayClusterProjectionStrings.h"

#include "Misc/DisplayClusterHelpers.h"
#include "Misc/FileHelper.h"

#include "WarpBlend/DisplayClusterWarpEnums.h"

struct FConfigParser
{
	FString  MPCDIFileName; // Single mpcdi file name

	FString  BufferId;
	FString  RegionId;

	FString  OriginType;

	// Support external pfm (warp)  and png(blend) files
	EDisplayClusterWarpProfileType  MPCDIType;

	FString  PFMFile;
	float    PFMFileScale;
	bool     bIsUnrealGameSpace;

	FString  AlphaFile;
	float AlphaGamma;

	FString  BetaFile;

	inline bool ImplLoadConfig(const TMap<FString, FString>& InConfigParameters)
	{
		// PFM file (optional)
		FString LocalPFMFile;
		if (DisplayClusterHelpers::map::template ExtractValue(InConfigParameters, DisplayClusterProjectionStrings::cfg::mpcdi::FilePFM, LocalPFMFile))
		{
			UE_LOG(LogDisplayClusterProjectionMPCDI, Log, TEXT("Found Argument '%s'='%s'"), DisplayClusterProjectionStrings::cfg::mpcdi::FilePFM, *LocalPFMFile);
			PFMFile = LocalPFMFile;
		}

		// Buffer
		if (!DisplayClusterHelpers::map::template ExtractValue(InConfigParameters, DisplayClusterProjectionStrings::cfg::mpcdi::Buffer, BufferId))
		{
			if (PFMFile.IsEmpty())
			{
				UE_LOG(LogDisplayClusterProjectionMPCDI, Error, TEXT("Argument '%s' not found in the config file"), DisplayClusterProjectionStrings::cfg::mpcdi::Buffer);
				return false;
			}
		}

		// Region
		if (!DisplayClusterHelpers::map::template ExtractValue(InConfigParameters, DisplayClusterProjectionStrings::cfg::mpcdi::Region, RegionId))
		{
			if (PFMFile.IsEmpty())
			{
				UE_LOG(LogDisplayClusterProjectionMPCDI, Error, TEXT("Argument '%s' not found in the config file"), DisplayClusterProjectionStrings::cfg::mpcdi::Region);
				return false;
			}
		}

		// Filename
		FString LocalMPCDIFileName;
		if (DisplayClusterHelpers::map::template ExtractValue(InConfigParameters, DisplayClusterProjectionStrings::cfg::mpcdi::File, LocalMPCDIFileName))
		{
			UE_LOG(LogDisplayClusterProjectionMPCDI, Log, TEXT("Found mpcdi file name for %s:%s - %s"), *BufferId, *RegionId, *LocalMPCDIFileName);
			MPCDIFileName = LocalMPCDIFileName;
		}

		// Origin node (optional)
		if (DisplayClusterHelpers::map::template ExtractValue(InConfigParameters, DisplayClusterProjectionStrings::cfg::mpcdi::Origin, OriginType))
		{
			UE_LOG(LogDisplayClusterProjectionMPCDI, Log, TEXT("Found origin node for %s:%s - %s"), *BufferId, *RegionId, *OriginType);
		}
		else
		{
			UE_LOG(LogDisplayClusterProjectionMPCDI, Log, TEXT("No origin node found for %s:%s. VR root will be used as default."), *BufferId, *RegionId);
		}

		{
			// MPCDIType (optional)
			FString MPCDITypeStr;
			if (!DisplayClusterHelpers::map::template ExtractValue(InConfigParameters, DisplayClusterProjectionStrings::cfg::mpcdi::MPCDIType, MPCDITypeStr))
			{
				MPCDIType = EDisplayClusterWarpProfileType::warp_A3D;
			}
			else
			{
				MPCDIType = EDisplayClusterWarpProfileType::Invalid;

				static const TArray<FString> Profiles({ "2d","3d","a3d","sl" });
				for (int i = 0; i < Profiles.Num(); ++i)
				{
					if (!MPCDITypeStr.Compare(Profiles[i], ESearchCase::IgnoreCase))
					{
						MPCDIType = (EDisplayClusterWarpProfileType)i;
						break;
					}
				}

				if (MPCDIType == EDisplayClusterWarpProfileType::Invalid)
				{
					UE_LOG(LogDisplayClusterProjectionMPCDI, Error, TEXT("Argument '%s' has unknown value '%s'"), DisplayClusterProjectionStrings::cfg::mpcdi::MPCDIType, *MPCDITypeStr);
					return false;
				}
			}

			// Default is UE scale, cm
			PFMFileScale = 1;
			if (DisplayClusterHelpers::map::template ExtractValueFromString(InConfigParameters, DisplayClusterProjectionStrings::cfg::mpcdi::WorldScale, PFMFileScale))
			{
				UE_LOG(LogDisplayClusterProjectionMPCDI, Log, TEXT("Found WorldScale value for %s:%s - %.f"), *BufferId, *RegionId, PFMFileScale);
			}

			bIsUnrealGameSpace = false;
			if (DisplayClusterHelpers::map::template ExtractValueFromString(InConfigParameters, DisplayClusterProjectionStrings::cfg::mpcdi::UseUnrealAxis, bIsUnrealGameSpace))
			{
				UE_LOG(LogDisplayClusterProjectionMPCDI, Log, TEXT("Found bIsUnrealGameSpace value for %s:%s - %s"), *BufferId, *RegionId, bIsUnrealGameSpace ? "true" : "false");
			}

			// AlphaFile file (optional)
			FString LocalAlphaFile;
			if (DisplayClusterHelpers::map::template ExtractValueFromString(InConfigParameters, DisplayClusterProjectionStrings::cfg::mpcdi::FileAlpha, LocalAlphaFile))
			{
				UE_LOG(LogDisplayClusterProjectionMPCDI, Log, TEXT("Found external AlphaMap file for %s:%s - %s"), *BufferId, *RegionId, *LocalAlphaFile);
				AlphaFile = LocalAlphaFile;
			}

			AlphaGamma = 1;
			if (DisplayClusterHelpers::map::template ExtractValueFromString(InConfigParameters, DisplayClusterProjectionStrings::cfg::mpcdi::AlphaGamma, AlphaGamma))
			{
				UE_LOG(LogDisplayClusterProjectionMPCDI, Log, TEXT("Found AlphaGamma value for %s:%s - %.f"), *BufferId, *RegionId, AlphaGamma);
			}

			// BetaFile file (optional)
			FString LocalBetaFile;
			if (DisplayClusterHelpers::map::template ExtractValueFromString(InConfigParameters, DisplayClusterProjectionStrings::cfg::mpcdi::FileBeta, LocalBetaFile))
			{
				UE_LOG(LogDisplayClusterProjectionMPCDI, Log, TEXT("Found external BetaMap file for %s:%s - %s"), *BufferId, *RegionId, *LocalBetaFile);
				BetaFile = LocalBetaFile;
			}
		}

		return true;
	}
};

