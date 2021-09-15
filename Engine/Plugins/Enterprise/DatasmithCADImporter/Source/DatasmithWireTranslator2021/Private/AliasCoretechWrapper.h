// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

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


// Defined the reference in which the object has to be defined
enum class EAliasObjectReference
{
	LocalReference,  
	ParentReference, 
	WorldReference, 
};


class FAliasCoretechWrapper : public CADLibrary::FCTSession
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
		: CADLibrary::FCTSession(InOwner)
	{
		// Unit for CoreTech session is set to cm, 0.01, because Wire's unit is cm. Consequently, Scale factor is set to 1.
		ImportParams.MetricUnit = 0.01;
		ImportParams.ScaleFactor = 1;
	}

	bool AddBRep(TArray<AlDagNode*>& DagNodeSet, EAliasObjectReference ObjectReference);

	static TSharedPtr<FAliasCoretechWrapper> GetSharedSession();

	bool Tessellate(FMeshDescription& Mesh, CADLibrary::FMeshParameters& MeshParameters);

protected:
	/**
	* Create a CT coedge (represent the use of an edge by a face).
	* @param TrimCurve: A curve in parametric surface space, part of a trim boundary.
	*/
	uint64 AddTrimCurve(AlTrimCurve& TrimCurve);
	uint64 AddTrimBoundary(AlTrimBoundary& TrimBoundary);
	uint64 Add3DCurve(AlCurve& Curve);

	uint64 AddTrimRegion(AlTrimRegion& InTrimRegion, EAliasObjectReference InObjectReference, AlMatrix4x4& InAlMatrix, bool bInOrientation);
	void AddFace(AlSurface& InSurface, EAliasObjectReference InObjectReference, AlMatrix4x4& InAlMatrix, bool bInOrientation, TArray<uint64>& OutFaceLis);
	void AddShell(AlShell& InShell, EAliasObjectReference InObjectReference, AlMatrix4x4& InAlMatrix, bool bInOrientation, TArray<uint64>& OutFaceLis);

protected:
	static TWeakPtr<FAliasCoretechWrapper> SharedSession;
	TMap<AlTrimCurve*, uint64>  AlEdge2CTEdge;
};

