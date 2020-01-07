// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/Vector.h"

namespace CADLibrary
{
	enum EStitchingTechnique
	{
		StitchingNone = 0,
		StitchingHeal,
		StitchingSew,
	};

	enum class EModelCoordSystem : uint8
	{
		ZUp_LeftHanded,
		ZUp_RightHanded,
		YUp_LeftHanded,
		YUp_RightHanded,
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
		EModelCoordSystem ModelCoordSys = EModelCoordSystem::ZUp_RightHanded;
		EDisplayDataPropagationMode Propagation = EDisplayDataPropagationMode::TopDown;
		EDisplayPreference DisplayPreference = EDisplayPreference::MaterialPrefered;
		bool bScaleUVMap = true;

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
	};

	struct FMeshParameters
	{
		bool bNeedSwapOrientation = false;
		bool bIsSymmetric = false;
		FVector SymmetricOrigin;
		FVector SymmetricNormal;
	};
}
