// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Util/ProgressCancel.h"
#include "ModelingOperators.h"


class MODELINGOPERATORS_API FSelfUnionMeshesOp : public FDynamicMeshOperator
{
public:
	virtual ~FSelfUnionMeshesOp() {}

	// inputs
	TSharedPtr<const FDynamicMesh3> CombinedMesh;
	bool bAttemptFixHoles;
	double WindingNumberThreshold = .5;
	bool bTrimFlaps;

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


