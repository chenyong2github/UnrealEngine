// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/Vector.h"
#include "DatasmithUtils.h"

namespace CADLibrary
{
	enum EStitchingTechnique
	{
		StitchingNone = 0,
		StitchingHeal,
		StitchingSew,
	};

	struct FImportParameters
	{
		double MetricUnit = 0.001;
		double ScaleFactor = 0.1;
		double ChordTolerance = 0.2;
		double MaxEdgeLength = 0.0;
		double MaxNormalAngle = 20.0;
		EStitchingTechnique StitchingTechnique = EStitchingTechnique::StitchingNone;
	};

	struct FMeshParameters
	{
		/** 
		 * CT need to work in mm (except with JT format in m). The mesh is scale at the static mesh build
		 */
		bool bNeedSwapOrientation = false;
		bool bIsSymmetric = false;
		FVector SymmetricOrigin;
		FVector SymmetricNormal;
		FDatasmithUtils::EModelCoordSystem ModelCoordSys = FDatasmithUtils::EModelCoordSystem::ZUp_RightHanded;
	};
}
