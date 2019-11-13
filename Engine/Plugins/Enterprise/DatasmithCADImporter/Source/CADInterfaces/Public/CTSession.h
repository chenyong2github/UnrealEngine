// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#ifdef CAD_INTERFACE

#include "CoreMinimal.h"
#include "CoreTechTypes.h"
#include "CADOptions.h"

struct FMeshDescription;

// Fill data array with a debug value (eg -1) to help debugging
#define MARK_UNINITIALIZED_MEMORY 0

namespace CADLibrary
{
class CADINTERFACES_API CTSession : public CoreTechSessionBase
{
public:
	/**
	 * Make sure CT is initialized, and a main object is ready.
	 * Handle input file unit and an output unit
	 * @param InOwner:        text that describe the owner of the session (helps to fix initialization issues)
	 * @param FileMetricUnit: number of meters per file unit.
	 * eg. For a file in inches, arg should be 0.0254
	 */
	CTSession(const TCHAR* InOwner, double InFileMetricUnit, double InScaleFactor)
		: CoreTechSessionBase(InOwner, InFileMetricUnit)
	{
		ImportParams.ScaleFactor = InScaleFactor;
		ImportParams.MetricUnit = InFileMetricUnit;
	}

	void ClearData();

	CheckedCTError SaveBrep(const FString& FilePath);
	CheckedCTError TopoFixes();

	/**
	 * In case of patch with cyclic boundary, a process has to be done by kernel_IO
	 */
	CheckedCTError CleanBRep();


	/**
	 * @param InScaleFactor : use to scale meshing from Kernel-IO
	 */
	void SetScaleFactor(double InScaleFactor)
	{
		ImportParams.ScaleFactor = InScaleFactor;
	}

	/**
	 * Set Import parameters,
	 * Tack care to set scale factor before because import parameters will be scale according to scale factor
	 * @param ChordTolerance : SAG	
	 * @param MaxEdgeLength : max length of element's edge
	 * @param NormalTolerance : Angle between two adjacent triangles
	 * @param StitchingTechnique : CAD topology correction technique
	 */
	void SetImportParameters(float ChordTolerance, float MaxEdgeLength, float NormalTolerance, CADLibrary::EStitchingTechnique StitchingTechnique, bool bScaleUVMap);
	
	CADLibrary::FImportParameters& GetImportParameters()
	{
		return ImportParams;
	}

protected:
	CADLibrary::FImportParameters ImportParams;
	static TWeakPtr<CTSession> SharedSession;
};

}

#endif // CAD_INTERFACE

