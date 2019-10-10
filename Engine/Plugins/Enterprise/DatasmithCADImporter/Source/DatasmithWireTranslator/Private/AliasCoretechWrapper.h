// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#ifdef CAD_LIBRARY
#include "CoreMinimal.h"


#include "CTSession.h"

class AlDagNode;
class AlShell;
class AlSurface;
class AlTrimBoundary;
class AlTrimCurve;
class AlTrimRegion;
class AlCurve;


typedef double AlMatrix4x4[4][4];

using namespace CADLibrary;

class FAliasCoretechWrapper : public CTSession
{
public:
	/**
	 * Make sure CT is initialized, and a main object is ready.
	 * Handle input file unit and an output unit
	 * @param InOwner
	 * @param FileMetricUnit number of meters per file unit.
	 * eg. For a file in inches, arg should be 0.0254
	 */
	FAliasCoretechWrapper(const TCHAR* InOwner)
		: CTSession(InOwner, 0.001, 1)
	{
	}

	CheckedCTError AddBRep(TArray<AlDagNode*>& DagNodeSet, bool bIsSymmetricBody);

	static TSharedPtr<FAliasCoretechWrapper> GetSharedSession();

protected:
	/**
	* Create a CT coedge (represent the use of an edge by a face).
	* @param TrimCurve: A curve in parametric surface space, part of a trim boundary.
	*/
	CT_OBJECT_ID AddTrimCurve(AlTrimCurve& TrimCurve);
	CT_OBJECT_ID AddTrimBoundary(AlTrimBoundary& TrimBoundary);
	CT_OBJECT_ID Add3DCurve(AlCurve& Curve);

	CT_OBJECT_ID AddTrimRegion(AlTrimRegion& TrimRegion, bool bIsSymmetricBody, bool bOrientation);
	void AddFace(AlSurface& Surface, CT_LIST_IO& FaceLis, bool bIsSymmetricBodyt, bool bOrientation);
	void AddShell(AlShell& Shell, CT_LIST_IO& FaceList, bool bIsSymmetricBody, bool bOrientation);

protected:
	static TWeakPtr<FAliasCoretechWrapper> SharedSession;
	TMap<AlTrimCurve*, CT_OBJECT_ID>  AlEdge2CTEdge;
};

#endif