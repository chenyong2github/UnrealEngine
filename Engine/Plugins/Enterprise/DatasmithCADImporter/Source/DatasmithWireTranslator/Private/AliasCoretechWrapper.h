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

// Defined the reference in which the object has to be defined
enum class EAliasObjectReference
{
	LocalReference,  
	ParentReference, 
	WorldReference, 
};


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
		: CTSession(InOwner, 0.01, 1) 
		// Unit for CoreTech session is set to cm, 0.01, because Wire's unit is cm. Consequently, Scale factor is set to 1.
	{
	}

	CT_IO_ERROR AddBRep(TArray<AlDagNode*>& DagNodeSet, EAliasObjectReference ObjectReference);

	static TSharedPtr<FAliasCoretechWrapper> GetSharedSession();

	CT_IO_ERROR Tessellate(FMeshDescription& Mesh, FMeshParameters& MeshParameters);

protected:
	/**
	* Create a CT coedge (represent the use of an edge by a face).
	* @param TrimCurve: A curve in parametric surface space, part of a trim boundary.
	*/
	CT_OBJECT_ID AddTrimCurve(AlTrimCurve& TrimCurve);
	CT_OBJECT_ID AddTrimBoundary(AlTrimBoundary& TrimBoundary);
	CT_OBJECT_ID Add3DCurve(AlCurve& Curve);

	CT_OBJECT_ID AddTrimRegion(AlTrimRegion& InTrimRegion, EAliasObjectReference InObjectReference, AlMatrix4x4& InAlMatrix, bool bInOrientation);
	void AddFace(AlSurface& InSurface, EAliasObjectReference InObjectReference, AlMatrix4x4& InAlMatrix, bool bInOrientation, CT_LIST_IO& OutFaceLis);
	void AddShell(AlShell& InShell, EAliasObjectReference InObjectReference, AlMatrix4x4& InAlMatrix, bool bInOrientation, CT_LIST_IO& OutFaceLis);

protected:
	static TWeakPtr<FAliasCoretechWrapper> SharedSession;
	TMap<AlTrimCurve*, CT_OBJECT_ID>  AlEdge2CTEdge;
};

#endif