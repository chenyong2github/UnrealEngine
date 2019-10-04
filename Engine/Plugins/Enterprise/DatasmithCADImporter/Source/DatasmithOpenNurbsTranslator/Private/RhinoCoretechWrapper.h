// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#ifdef CAD_LIBRARY
#include "CoreMinimal.h"

#include "CTSession.h"

class ON_Brep;

namespace CADLibrary
{
	class FRhinoCoretechWrapper : public CTSession
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

		CheckedCTError AddBRep(ON_Brep& brep);
		static TSharedPtr<FRhinoCoretechWrapper> GetSharedSession(double SceneUnit, double ScaleFactor);

	protected:
		static TWeakPtr<FRhinoCoretechWrapper> SharedSession;
	};

} // namespace CADLibrary
#endif