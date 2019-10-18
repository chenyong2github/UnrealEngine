// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Util/ProgressCancel.h"
#include "ModelingOperators.h"


#include "SimplifyMeshOp.generated.h"

class FDynamicMesh3;
struct FMeshDescription;

template <class TriangleMeshType>
class TMeshAABBTree3;
typedef TMeshAABBTree3<FDynamicMesh3> FDynamicMeshAABBTree3;


UENUM()
enum class ESimplifyTargetType : uint8
{
	/** Percentage of input triangles */
	Percentage = 0 UMETA(DisplayName = "Percentage"),

	/** Target triangle count */
	TriangleCount = 1 UMETA(DisplayName = "Triangle Count"),

	/** Target edge length */
	EdgeLength = 2 UMETA(DisplayName = "Edge Length")
};

UENUM()
enum class ESimplifyType : uint8
{
	/** Fastest. Standard quadric error metric. Will not simplify UV boundaries.*/
	QEM = 0 UMETA(DisplayName = "QEM"),

	/** Potentially higher quality. Takes the normal into account. Will not simplify UV bounaries. */
	Attribute = 1 UMETA(DisplayName = "Normal Aware"),

	/** Highest quality reduction.  Will simplify UV boundaries. */
	UE4Standard = 2 UMETA(DisplayName = "UE4 Standard"),
};





class MODELINGOPERATORSEDITORONLY_API FSimplifyMeshOp : public FDynamicMeshOperator
{
public:
	virtual ~FSimplifyMeshOp() {}

	//
	// Inputs
	// 
	ESimplifyTargetType TargetMode;
	ESimplifyType SimplifierType;
	int TargetPercentage, TargetCount;
	float TargetEdgeLength;
	bool bDiscardAttributes, bReproject, bPreventNormalFlips;

	// stored for the UE4 Standard path
	TSharedPtr<FMeshDescription> OriginalMeshDescription;
	// stored for the GeometryProcessing custom simplifier paths (currently precomputed once in tool setup)
	TSharedPtr<FDynamicMesh3> OriginalMesh;
	TSharedPtr<FDynamicMeshAABBTree3> OriginalMeshSpatial;

	class IMeshReduction* MeshReduction;


	// set ability on protected transform.
	void SetTransform(const FTransform& Transform)
	{
		ResultTransform = (FTransform3d)Transform;
	}

	//
	// FDynamicMeshOperator implementation
	// 

	virtual void CalculateResult(FProgressCancel* Progress) override;
};
