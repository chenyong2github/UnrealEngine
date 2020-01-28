// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MeshConstraints.h"
#include "Util/ProgressCancel.h"
#include "ModelingOperators.h"

#include "RemeshMeshOp.generated.h"

class FDynamicMesh3;

template <class TriangleMeshType>
class TMeshAABBTree3;
typedef TMeshAABBTree3<FDynamicMesh3> FDynamicMeshAABBTree3;

/** Remeshing modes */
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

class MODELINGOPERATORS_API FRemeshMeshOp : public FDynamicMeshOperator
{
public:
	virtual ~FRemeshMeshOp() {}

	// inputs
	TSharedPtr<FDynamicMesh3> OriginalMesh;
	TSharedPtr<FDynamicMeshAABBTree3> OriginalMeshSpatial;

	int RemeshIterations;
	float SmoothingStrength, TargetEdgeLength;
	ERemeshSmoothingType SmoothingType;
	bool bDiscardAttributes, bPreserveSharpEdges, bFlips, bSplits, bCollapses, bReproject, bPreventNormalFlips;
	EEdgeRefineFlags MeshBoundaryConstraint, GroupBoundaryConstraint, MaterialBoundaryConstraint;
	void SetTransform(const FTransform& Transform);

	//
	// FDynamicMeshOperator implementation
	// 
	virtual void CalculateResult(FProgressCancel* Progress) override;
};


