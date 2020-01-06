// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// Port of geometry3Sharp MeshPlaneCut

#pragma once

#include "MathUtil.h"
#include "VectorTypes.h"
#include "GeometryTypes.h"
#include "MeshBoundaryLoops.h"
#include "Curve/GeneralPolygon2.h"

class FDynamicMesh3;
template<typename RealType>
class TDynamicMeshScalarTriangleAttribute;

/**
 * Cut the Mesh with the Plane. The *positive* side, ie (p-o).n > 0, is removed.
 * If possible, returns boundary loop(s) along cut
 * (this will fail if cut intersected with holes in mesh).
 * Also FillHoles() for a topological fill. Or use CutLoops and fill yourself.
 * 
 * Algorithm is:
 *    1) find all edge crossings
 *	  2) optionally discard any triangles with all vertex distances < epsilon.
 *    3) Do edge splits at crossings
 *    4 option a) (optionally) delete all vertices on positive side
 *	  4 option b) (OR optionally) disconnect all triangles w/ vertices on positive side (if keeping both sides)
 *	  4 option c) do nothing (if keeping both sides and not disconnecting them)
 *    5) (optionally) collapse any degenerate boundary edges 
 *	  6) (optionally) change an attribute tag for all triangles on positive side
 *    7) find loops through valid boundary edges (ie connected to splits, or on-plane edges) (if second half was kept, do this separately for each separate mesh ID label)
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

	/** Tolerance distance for considering a vertex to be 'on plane' */
	double PlaneTolerance = FMathf::ZeroTolerance * 10.0;


	// TODO support optionally restricting plane cut to a mesh selection
	//MeshFaceSelection CutFaceSet;

	//
	// Outputs
	//
	struct FOpenBoundary
	{
		int Label; // optional ID, used to transfer label to new hole-fill triangles
		float NormalSign = 1; // -1 for the open boundary on the other side of the cut (for the CutWithoutDelete path)
		TArray<FEdgeLoop> CutLoops;
		TArray<FEdgeSpan> CutSpans;
		bool CutLoopsFailed = false;		// set to true if we could not compute cut loops/spans
		bool FoundOpenSpans = false;     // set to true if we found open spans in cut
	};
	// note: loops and spans within a single FOpenBoundary could be part of the same hole-fill triangulation
	//	separate open boundary structs will be considered separately and will not share hole fill triangles
	TArray<FOpenBoundary> OpenBoundaries;

	// Triangle IDs of hole fill triangles.  Outer array is 1:1 with the OpenBoundaries array
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

	// split plane-crossing edges and remove geometry on the positive side of the cutting plane
	virtual bool Cut();

	// alternate path for splitting the mesh without deleting the other half
	virtual bool CutWithoutDelete(bool bSplitVerticesAtPlane, float OffsetSeparatedPortion = 0.0f,
		TDynamicMeshScalarTriangleAttribute<int>* TriLabels = nullptr, int NewLabelStartID = 0,
		bool bAddBoundariesFirstHalf = true, bool bAddBoundariesSecondHalf = true);

	/**
	 *  Fill cut loops with FSimpleHoleFiller
	 */
	virtual bool SimpleHoleFill(int ConstantGroupID = -1);

	/**
	 *  Fill cut loops with FPlanarHoleFiller, using a caller-provided triangulation function
	 */
	virtual bool HoleFill(TFunction<TArray<FIndex3i>(const FGeneralPolygon2d&)> PlanarTriangulationFunc, bool bFillSpans, int ConstantGroupID = -1);

	
	virtual void TransferTriangleLabelsToHoleFillTriangles(TDynamicMeshScalarTriangleAttribute<int>* TriLabels);

protected:

	void CollapseDegenerateEdges(const TSet<int>& OnCutEdges, const TSet<int>& ZeroEdges);
	void SplitCrossingEdges(TArray<double>& Signs, TSet<int>& ZeroEdges, TSet<int>& OnCutEdges, bool bDeleteTrisOnPlane = true);
	bool ExtractBoundaryLoops(const TSet<int>& OnCutEdges, const TSet<int>& ZeroEdges, FMeshPlaneCut::FOpenBoundary& Boundary);
};
