// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DatasmithUtils.h"
#include "HAL/IConsoleManager.h"
#include "Math/Vector.h"
#include "Misc/Paths.h"
#include "Templates/TypeHash.h"

namespace CADLibrary
{
	CADTOOLS_API extern int32 GMaxImportThreads;

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

	class FImportParameters
	{
	private:
		double MetricUnit;
		double ScaleFactor;
		double ChordTolerance;
		double MaxEdgeLength;
		double MaxNormalAngle;
		EStitchingTechnique StitchingTechnique;
		FDatasmithUtils::EModelCoordSystem ModelCoordSys;
		EDisplayDataPropagationMode Propagation;
		EDisplayPreference DisplayPreference;
		bool bScaleUVMap;
		
	public:
		CADTOOLS_API static bool bGDisableCADKernelTessellation;
		CADTOOLS_API static bool bGEnableTimeControl;
		CADTOOLS_API static bool bGEnableCADCache;
		CADTOOLS_API static bool bGOverwriteCache;
		CADTOOLS_API static bool bGPreferJtFileEmbeddedTessellation;
		CADTOOLS_API static float GStitchingTolerance;
		CADTOOLS_API static FString GCADLibrary;

	public:
		FImportParameters(double InMetricUnit = 0.001, double InScaleFactor = 1., FDatasmithUtils::EModelCoordSystem NewCoordinateSystem = FDatasmithUtils::EModelCoordSystem::ZUp_RightHanded)
			: MetricUnit(InMetricUnit)
			, ScaleFactor(InScaleFactor)
			, ChordTolerance(0.2)
			, MaxEdgeLength(0.0)
			, MaxNormalAngle(20.0)
			, StitchingTechnique(EStitchingTechnique::StitchingNone)
			, ModelCoordSys(NewCoordinateSystem)
			, Propagation(EDisplayDataPropagationMode::TopDown)
			, DisplayPreference(EDisplayPreference::MaterialPrefered)
			, bScaleUVMap(true)
		{
		}
	
		/**
		 * @param ValueInMM, the value in millimeter to convert into the scene metric unit 
		 */
		double ConvertMMToImportUnit(double ValueInMM) const
		{
			return ValueInMM * 0.001 / MetricUnit;
		}

		void SetTesselationParameters(double InChordTolerance, double InMaxEdgeLength, double InMaxNormalAngle, CADLibrary::EStitchingTechnique InStitchingTechnique)
		{
			ChordTolerance = InChordTolerance;
			MaxEdgeLength = InMaxEdgeLength;
			MaxNormalAngle = InMaxNormalAngle;
			StitchingTechnique = InStitchingTechnique;
		}

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
				Hash = HashCombine(Hash, ::GetTypeHash(Param));
			}
			for (uint32 Param : {uint32(StitchingTechnique), uint32(ModelCoordSys), uint32(Propagation), uint32(DisplayPreference)})
			{
				Hash = HashCombine(Hash, ::GetTypeHash(Param));
			}
			Hash = HashCombine(Hash, ::GetTypeHash(bScaleUVMap));
			Hash = HashCombine(Hash, ::GetTypeHash(bGPreferJtFileEmbeddedTessellation));
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
			// these 5 static variables have to be serialized to be transmitted to CADWorkers
			// because CADWorker has not access to CVars
			Ar << ImportParameters.bGOverwriteCache;
			Ar << ImportParameters.bGDisableCADKernelTessellation;
			Ar << ImportParameters.bGEnableTimeControl;
			Ar << ImportParameters.bGEnableCADCache;
			Ar << ImportParameters.bGPreferJtFileEmbeddedTessellation;
			Ar << ImportParameters.GStitchingTolerance;
			Ar << ImportParameters.GCADLibrary;
			return Ar;
		}

		double GetMetricUnit() const
		{
			return MetricUnit;
		}

		double GetScaleFactor() const
		{
			return ScaleFactor;
		}

		double GetChordTolerance() const
		{
			return ChordTolerance;
		}

		double GetMaxNormalAngle() const
		{
			return MaxNormalAngle;
		}

		double GetMaxEdgeLength() const
		{
			return MaxEdgeLength;
		}

		EStitchingTechnique GetStitchingTechnique() const
		{
			return StitchingTechnique;
		}

		FDatasmithUtils::EModelCoordSystem GetModelCoordSys() const
		{
			return ModelCoordSys;
		}

		EDisplayDataPropagationMode GetPropagation() const
		{
			return Propagation;
		}

		EDisplayPreference GetDisplayPreference() const
		{
			return DisplayPreference;
		}

		bool NeedScaleUVMap() const
		{	
			return bScaleUVMap;
		}

		void SwitchOffUVMapScaling()
		{
			bScaleUVMap = false;
		}

		void SetModelCoordinateSystem(FDatasmithUtils::EModelCoordSystem NewCoordinateSystem)
		{
			ModelCoordSys = NewCoordinateSystem;
		}

		void SetDisplayPreference(CADLibrary::EDisplayPreference InDisplayPreference)
		{
			DisplayPreference = InDisplayPreference;
		}

		void SetPropagationMode(CADLibrary::EDisplayDataPropagationMode InMode)
		{
			Propagation = InMode;
		}

		CADTOOLS_API friend uint32 GetTypeHash(const FImportParameters& ImportParameters);

	};

	inline FString BuildCacheFilePath(const TCHAR* CachePath, const TCHAR* Folder, uint32 BodyHash)
	{
		FString BodyFileName = FString::Printf(TEXT("UEx%08x"), BodyHash);
		FString OutFileName = FPaths::Combine(CachePath, Folder, BodyFileName);

		if (FImportParameters::bGDisableCADKernelTessellation)
		{
			OutFileName += FImportParameters::GCADLibrary.Equals("TechSoft") ? TEXT(".prc") : TEXT(".ct");
		}
		else
		{
			OutFileName += TEXT(".ugeom");
		}
		return OutFileName;
	}

	struct FMeshParameters
	{
		bool bNeedSwapOrientation = false;
		bool bIsSymmetric = false;
		FVector SymmetricOrigin;
		FVector SymmetricNormal;
	};
}
