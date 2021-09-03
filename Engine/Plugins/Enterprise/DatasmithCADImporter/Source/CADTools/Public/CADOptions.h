// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DatasmithUtils.h"
#include "HAL/IConsoleManager.h"
#include "Math/Vector.h"
#include "Misc/Paths.h"
#include "Templates/TypeHash.h"

namespace CADLibrary
{
	CADTOOLS_API extern bool bGDisableCADKernelTessellation;
	CADTOOLS_API extern bool bGEnableCADCache;
	CADTOOLS_API extern bool bGEnableTimeControl;
	CADTOOLS_API extern bool bGOverwriteCache;
	CADTOOLS_API extern int32 GMaxImportThreads;
	CADTOOLS_API extern FString GCADLibrary;

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
		bool bEnableSequentialImport = true;
		bool bOverwriteCache = false;
		bool bDisableCADKernelTessellation = false;
		bool bEnableTimeControl = true;

		void SetMetricUnit(double NewMetricUnit)
		{
			MetricUnit = NewMetricUnit;
			ScaleFactor = NewMetricUnit / 0.01;
		}

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
			Ar << ImportParameters.bEnableSequentialImport;
			Ar << ImportParameters.bOverwriteCache;
			Ar << ImportParameters.bDisableCADKernelTessellation;
			return Ar;
		}

		FString DefineCADFilePath(const TCHAR* FolderPath, const TCHAR* InFileName) const
		{
			FString OutFileName = FPaths::Combine(FolderPath, InFileName);
			if (bDisableCADKernelTessellation)
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
