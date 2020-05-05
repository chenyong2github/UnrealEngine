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

	/** Trim the first object using the second */
	TrimA = 4 UMETA(DisplayName = "Trim A"),

	/** Trim the second object using the first */
	TrimB = 5 UMETA(DisplayName = "Trim B"),

};



class MODELINGOPERATORS_API FBooleanMeshesOp : public FDynamicMeshOperator
{
public:
	virtual ~FBooleanMeshesOp() {}

	// inputs
	ECSGOperation Operation;
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


