// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DatasmithUtils.h"
#include "Math/Vector.h"
#include "Misc/Paths.h"
#include "Templates/TypeHash.h"

namespace CADLibrary
{
	enum EStitchingTechnique
	{
		StitchingNone = 0,
		StitchingHeal,
		StitchingSew,
	};

	enum class EDisplayPreference : uint8
	{
		ColorPrefered,
		MaterialPrefered,
		ColorOnly,
		MaterialOnly,
	};

	enum class EDisplayDataPropagationMode : uint8
	{
		TopDown,
		BottomUp,
		BodyOnly,
	};

	struct FImportParameters
	{
		double MetricUnit = 0.001;
		double ScaleFactor = 0.1;
		double ChordTolerance = 0.2;
		double MaxEdgeLength = 0.0;
		double MaxNormalAngle = 20.0;
		EStitchingTechnique StitchingTechnique = EStitchingTechnique::StitchingNone;
		FDatasmithUtils::EModelCoordSystem ModelCoordSys = FDatasmithUtils::EModelCoordSystem::ZUp_RightHanded;
		EDisplayDataPropagationMode Propagation = EDisplayDataPropagationMode::TopDown;
		EDisplayPreference DisplayPreference = EDisplayPreference::MaterialPrefered;
		bool bScaleUVMap = true;
		bool bEnableCacheUsage = true;
		bool bEnableKernelIOTessellation = true;
		bool bEnableTimeControl = true;

		uint32 GetHash() const
		{
			uint32 Hash = 0; 
			for (double Param : {MetricUnit, ScaleFactor, ChordTolerance, MaxEdgeLength, MaxNormalAngle})
			{
				Hash = HashCombine(Hash, GetTypeHash(Param));
			}
			for (uint32 Param : {uint32(StitchingTechnique), uint32(ModelCoordSys), uint32(Propagation), uint32(DisplayPreference)})
			{
				Hash = HashCombine(Hash, GetTypeHash(Param));
			}
			Hash = HashCombine(Hash, GetTypeHash(bScaleUVMap));
			return Hash;
		}

		friend FArchive& operator<<(FArchive& Ar, FImportParameters& ImportParameters)
		{
			Ar << ImportParameters.MetricUnit;
			Ar << ImportParameters.ScaleFactor;
			Ar << ImportParameters.ChordTolerance;
			Ar << ImportParameters.MaxEdgeLength;
			Ar << ImportParameters.MaxNormalAngle;
			Ar << (uint32&) ImportParameters.StitchingTechnique;
			Ar << (uint8&) ImportParameters.ModelCoordSys;
			Ar << (uint8&) ImportParameters.Propagation;
			Ar << (uint8&) ImportParameters.DisplayPreference;
			Ar << ImportParameters.bScaleUVMap;
			Ar << ImportParameters.bEnableCacheUsage;
			Ar << ImportParameters.bEnableKernelIOTessellation;
			return Ar;
		}

		FString DefineCADFilePath(const TCHAR* FolderPath, const TCHAR* InFileName) const
		{
			FString OutFileName = FPaths::Combine(FolderPath, InFileName);
			if (bEnableKernelIOTessellation)
			{
				OutFileName += TEXT(".ct");
			}
			else
			{
				OutFileName += TEXT(".ugeom");
			}
			return OutFileName;
		}
	};

	struct FMeshParameters
	{
		bool bNeedSwapOrientation = false;
		bool bIsSymmetric = false;
		FVector SymmetricOrigin;
		FVector SymmetricNormal;
	};
}
