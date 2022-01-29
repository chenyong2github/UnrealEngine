// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "MeshConstraints.h"
#include "Util/ProgressCancel.h"
#include "ModelingOperators.h"

#include "RemeshMeshOp.generated.h"

/** Remeshing modes */
UENUM()
enum class ERemeshType : uint8
{
	/** One pass over the entire mesh, then remesh only changed edges */
	Standard = 0 UMETA(DisplayName = "Standard"),

	/** Multiple full passes over the entire mesh */
	FullPass = 1 UMETA(DisplayName = "Full Pass"),

	/** One pass over the entire mesh, then remesh only changed edges. Use Normal flow to align triangles with input.*/
	NormalFlow = 2 UMETA(DisplayName = "Normal Flow"),

};

/** Smoothing modes */
UENUM()
enum class ERemeshSmoothingType : uint8
{
	/** Uniform Smoothing */
	Uniform = 0 UMETA(DisplayName = "Uniform"),

	/** Cotangent Smoothing */
	Cotangent = 1 UMETA(DisplayName = "Shape Preserving"),

	/** Mean Value Smoothing */
	MeanValue = 2 UMETA(DisplayName = "Mixed")
};

namespace UE
{
namespace Geometry
{

class FRemesher;

class MODELINGOPERATORS_API FRemeshMeshOp : public FDynamicMeshOperator
{
public:
	virtual ~FRemeshMeshOp() {}

	// inputs
	TSharedPtr<FDynamicMesh3, ESPMode::ThreadSafe> OriginalMesh;
	TSharedPtr<FDynamicMeshAABBTree3, ESPMode::ThreadSafe> OriginalMeshSpatial;

	ERemeshType RemeshType = ERemeshType::Standard;

	int RemeshIterations;
	int MaxRemeshIterations;
	int ExtraProjectionIterations = 5;
	int TriangleCountHint = 0;
	float SmoothingStrength, TargetEdgeLength;
	ERemeshSmoothingType SmoothingType;
	bool bDiscardAttributes, bPreserveSharpEdges, bFlips, bSplits, bCollapses, bReproject, bPreventNormalFlips;
	// When true, result will have attributes object regardless of whether attributes 
	// were discarded or present initially.
	bool bResultMustHaveAttributesEnabled = false;
	EEdgeRefineFlags MeshBoundaryConstraint, GroupBoundaryConstraint, MaterialBoundaryConstraint;

	FDynamicMesh3* ProjectionTarget = nullptr;
	FDynamicMeshAABBTree3* ProjectionTargetSpatial = nullptr;

	FTransformSRT3d TargetMeshLocalToWorld;
	FTransformSRT3d ToolMeshLocalToWorld;
	bool bUseWorldSpace = false;
	bool bParallel = true;

	// Normal flow only:

	/// During each call to RemeshIteration, do this many passes of face-aligned projection
	int FaceProjectionPassesPerRemeshIteration = 1;

	/// drag on surface projection
	double SurfaceProjectionSpeed = 0.2;

	/// drag on normal alignment
	double NormalAlignmentSpeed = 0.2;

	/// Control whether or not we want to apply mesh smoothing in "free" areas that have not projected to target surface.
	/// This smoothing is on applied in the ExtraProjections Iterations
	bool bSmoothInFillAreas = true;

	/// This is used as a multiplier on MaxEdgeLength to determine when we identify points as being in "free" areas
	float FillAreaDistanceMultiplier = 0.25;

	/// This is used as a multiplier on the Remesher smoothing rate, applied to points identified as being in "free" areas
	float FillAreaSmoothMultiplier = 0.25;

	// End normal flow only


	void SetTransform(const FTransformSRT3d& Transform);

	//
	// FDynamicMeshOperator implementation
	// 
	virtual void CalculateResult(FProgressCancel* Progress) override;

protected:

	TUniquePtr<FRemesher> CreateRemesher(ERemeshType Type, FDynamicMesh3* TargetMesh);

};

} // end namespace UE::Geometry
} // end namespace UE


