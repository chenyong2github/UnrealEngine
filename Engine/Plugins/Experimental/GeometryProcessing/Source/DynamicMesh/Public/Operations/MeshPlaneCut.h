// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// Port of geometry3Sharp MeshPlaneCut

#pragma once

#include "MathUtil.h"
#include "VectorTypes.h"
#include "GeometryTypes.h"
#include "MeshBoundaryLoops.h"
#include "Curve/GeneralPolygon2.h"


class FDynamicMesh3;


/**
 * Cut the Mesh with the Plane. The *positive* side, ie (p-o).n > 0, is removed.
 * If possible, returns boundary loop(s) along cut
 * (this will fail if cut intersected with holes in mesh).
 * Also FillHoles() for a topological fill. Or use CutLoops and fill yourself.
 * 
 * Algorithm is:
 *    1) find all edge crossings
 *    2) Do edge splits at crossings
 *    3) delete all vertices on positive side
 *    4) (optionally) collapse any degenerate boundary edges 
 *    5) find loops through valid boundary edges (ie connected to splits, or on-plane edges)
 * 
 * @todo Also discard any triangles with all vertex distances < epsilon.
 */
class DYNAMICMESH_API FMeshPlaneCut
{
public:

	//
	// Inputs
	//
	FDynamicMesh3 *Mesh;
	FVector3d PlaneOrigin, PlaneNormal;
	
	bool bCollapseDegenerateEdgesOnCut = true;
	double DegenerateEdgeTol = FMathd::ZeroTolerance;

	/** UVs on any hole fill surfaces are scaled by this amount */
	float UVScaleFactor = 1.0f;

	// TODO support optionally restricting plane cut to a mesh selection
	//MeshFaceSelection CutFaceSet;

	//
	// Outputs
	//
	TArray<FEdgeLoop> CutLoops;
	TArray<FEdgeSpan> CutSpans;
	bool CutLoopsFailed = false;		// set to true if we could not compute cut loops/spans
	bool FoundOpenSpans = false;     // set to true if we found open spans in cut

	// Note: In practice this is 1:1 with CutLoops if all the hole fills succeed w/ simple hole filling, but this will not be true for other hole filling methods, or if any of the hole fills fails
	// please do not write anything relying on a correspondence between the two
	TArray<TArray<int>> HoleFillTriangles;


public:

	/**
	 *  Cut mesh with plane. Assumption is that plane normal is Z value.
	 */
	FMeshPlaneCut(FDynamicMesh3* Mesh, FVector3d Origin, FVector3d Normal) : Mesh(Mesh), PlaneOrigin(Origin), PlaneNormal(Normal)
	{
	}
	virtual ~FMeshPlaneCut() {}
	
	/**
	 * @return EOperationValidationResult::Ok if we can apply operation, or error code if we cannot
	 */
	virtual EOperationValidationResult Validate()
	{
		// @todo validate inputs
		return EOperationValidationResult::Ok;
	}

	virtual bool Cut();

	/**
	 *  Fill cut loops with FSimpleHoleFiller
	 */
	virtual bool SimpleHoleFill(int ConstantGroupID = -1);

	/**
	 *  Fill cut loops with FPlanarHoleFiller, using a caller-provided triangulation function
	 */
	virtual bool HoleFill(TFunction<TArray<FIndex3i>(const FGeneralPolygon2d&)> PlanarTriangulationFunc, bool bFillSpans, int ConstantGroupID = -1);

	
protected:

	void CollapseDegenerateEdges(const TSet<int>& OnCutEdges, const TSet<int>& ZeroEdges);
};
