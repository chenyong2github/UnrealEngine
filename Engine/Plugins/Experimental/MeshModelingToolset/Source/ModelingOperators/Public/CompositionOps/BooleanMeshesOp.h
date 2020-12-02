// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Util/ProgressCancel.h"
#include "ModelingOperators.h"


#include "BooleanMeshesOp.generated.h"


/** CSG operation types */
UENUM()
enum class ECSGOperation : uint8
{
	/** Subtracts the first object from the second */
	DifferenceAB = 0 UMETA(DisplayName = "A - B"),

	/** Subtracts the second object from the first */
	DifferenceBA = 1 UMETA(DisplayName = "B - A"),

	/** intersection of two objects */
	Intersect = 2 UMETA(DisplayName = "Intersect"),

	/** union of two objects */
	Union = 3 UMETA(DisplayName = "Union"),
};

UENUM()
enum class ETrimOperation : uint8
{
	/** Remove geometry from the first object using the second */
	TrimA = 0 UMETA(DisplayName = "Trim A"),

	/** Remove geometry from the second object using the first */
	TrimB = 1 UMETA(DisplayName = "Trim B"),
};


UENUM()
enum class ETrimSide : uint8
{
	RemoveInside = 0,
	RemoveOutside = 1,
};


class MODELINGOPERATORS_API FBooleanMeshesOp : public FDynamicMeshOperator
{
public:
	virtual ~FBooleanMeshesOp() {}

	// inputs
	ECSGOperation CSGOperation;
	ETrimOperation TrimOperation;
	ETrimSide TrimSide;
	bool bTrimMode = false; // if true, do a trim operation instead of a boolean
	TArray<TSharedPtr<const FDynamicMesh3>> Meshes;
	TArray<FTransform> Transforms; // 1:1 with Meshes
	bool bAttemptFixHoles;

	void SetTransform(const FTransform& Transform);

	//
	// FDynamicMeshOperator implementation
	// 

	virtual void CalculateResult(FProgressCancel* Progress) override;

	// IDs of any newly-created boundary edges in the result mesh
	TArray<int> GetCreatedBoundaryEdges() const
	{
		return CreatedBoundaryEdges;
	}

private:
	TArray<int> CreatedBoundaryEdges;
};


