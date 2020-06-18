// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ModelingOperators.h"
#include "EdgeLoop.h"
#include "Operations/SmoothHoleFiller.h"
#include "HoleFillOp.generated.h"

UENUM()
enum class EHoleFillOpFillType : uint8
{
	/** Fill with a fan of triangles connected to a new central vertex. */
	TriangleFan UMETA(DisplayName = "TriangleFan"),

	/** Incrementally triangulate the hole boundary without introducing new interior vertices. */
	PolygonEarClipping UMETA(DisplayName = "PolygonEarClipping"),

	/** Choose a best-fit plane, project the boundary vertices to the plane, and use 2D Delaunay triangulation. */
	Planar UMETA(DisplayName = "Planar"),

	/** Fill with a triangulation which attempts to minimize Gaussian curvature and not introduce new interior vertices. */
	Minimal UMETA(DisplayName = "Minimal"),

	/** Fill hole with a simple triangulation, then alternate between smoothing and remeshing. Optionally include the
	    triangles around the hole in the smoothing/remeshing. */
	Smooth UMETA(DisplayName = "Smooth")
};


struct FFillOptions
{
	bool bRemoveIsolatedTriangles = false;
};

class MODELINGOPERATORS_API FHoleFillOp : public FDynamicMeshOperator
{
public:

	// inputs
	
	TSharedPtr<FDynamicMesh3> OriginalMesh;		// Ownership shared with Tool

	EHoleFillOpFillType FillType = EHoleFillOpFillType::Minimal;
	double MeshUVScaleFactor = 1.0;
	TArray<FEdgeLoop> Loops;

	FFillOptions FillOptions;
	FSmoothFillOptions SmoothFillOptions;

	// output
	TArray<int32> NewTriangles;
	int NumFailedLoops = 0;

	// FDynamicMeshOperator implementation
	void CalculateResult(FProgressCancel* Progress) override;

};

