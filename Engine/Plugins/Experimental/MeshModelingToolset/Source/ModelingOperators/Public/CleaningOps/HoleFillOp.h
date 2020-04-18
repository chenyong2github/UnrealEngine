// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ModelingOperators.h"
#include "EdgeLoop.h"
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

	/** Fill with a triangulation which attempts to minimize Gaussian curvature and not introcude new interior vertices. */
	Minimal UMETA(DisplayName = "Minimal")
};


class MODELINGOPERATORS_API FHoleFillOp : public FDynamicMeshOperator
{
public:

	// inputs
	const FDynamicMesh3* OriginalMesh = nullptr;
	EHoleFillOpFillType FillType = EHoleFillOpFillType::TriangleFan;
	double MeshUVScaleFactor = 1.0;
	TArray<FEdgeLoop> Loops;

	// output
	TArray<int32> NewTriangles;

	// FDynamicMeshOperator implementation
	void CalculateResult(FProgressCancel* Progress) override;
};

