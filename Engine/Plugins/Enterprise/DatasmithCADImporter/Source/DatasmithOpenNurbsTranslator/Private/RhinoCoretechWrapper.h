// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#ifdef CAD_LIBRARY
#include "CoreMinimal.h"

#include "CTSession.h"

class ON_Brep;

class FRhinoCoretechWrapper : public CADLibrary::CTSession
{
public:
	/**
	 * Make sure CT is initialized, and a main object is ready.
	 * Handle input file unit and an output unit
	 * @param InOwner
	 * @param FileMetricUnit number of meters per file unit.
	 * @param ScaleFactor scale factor to apply to the mesh to be in UE4 unit (cm).
	 * eg. For a file in inches, arg should be 0.0254
	 */
	FRhinoCoretechWrapper(const TCHAR* InOwner, double FileMetricUnit, double ScaleFactor)
		: CTSession(InOwner, FileMetricUnit, ScaleFactor)
	{
	}

	CADLibrary::CheckedCTError AddBRep(ON_Brep& brep);
	static TSharedPtr<FRhinoCoretechWrapper> GetSharedSession(double SceneUnit, double ScaleFactor);

	CT_IO_ERROR Tessellate(FMeshDescription& Mesh, CADLibrary::FMeshParameters& MeshParameters);

protected:
	static TWeakPtr<FRhinoCoretechWrapper> SharedSession;
};

#endif