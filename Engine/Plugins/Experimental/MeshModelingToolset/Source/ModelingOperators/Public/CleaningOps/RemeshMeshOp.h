// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MeshConstraints.h"
#include "Util/ProgressCancel.h"
#include "ModelingOperators.h"

#include "RemeshMeshOp.generated.h"

class FDynamicMesh3;
class FRemesher;

template <class TriangleMeshType>
class TMeshAABBTree3;
typedef TMeshAABBTree3<FDynamicMesh3> FDynamicMeshAABBTree3;

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

class MODELINGOPERATORS_API FRemeshMeshOp : public FDynamicMeshOperator
{
public:
	virtual ~FRemeshMeshOp() {}
	
	// inputs
	TSharedPtr<FDynamicMesh3> OriginalMesh;
	TSharedPtr<FDynamicMeshAABBTree3> OriginalMeshSpatial;

	ERemeshType RemeshType = ERemeshType::Standard;

	int RemeshIterations;
	int MaxRemeshIterations;
	int ExtraProjectionIterations = 5;
	float SmoothingStrength, TargetEdgeLength;
	ERemeshSmoothingType SmoothingType;
	bool bDiscardAttributes, bPreserveSharpEdges, bFlips, bSplits, bCollapses, bReproject, bPreventNormalFlips;
	EEdgeRefineFlags MeshBoundaryConstraint, GroupBoundaryConstraint, MaterialBoundaryConstraint;

	FDynamicMesh3* ProjectionTarget = nullptr;
	FDynamicMeshAABBTree3* ProjectionTargetSpatial = nullptr;

	void SetTransform(const FTransform& Transform);

	//
	// FDynamicMeshOperator implementation
	// 
	virtual void CalculateResult(FProgressCancel* Progress) override;

protected:

	TUniquePtr<FRemesher> CreateRemesher(ERemeshType Type, FDynamicMesh3* TargetMesh);

};


