// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Util/ProgressCancel.h"
#include "ModelingOperators.h"

#include "BaseOps/VoxelBaseOp.h"



class MODELINGOPERATORS_API FVoxelSolidifyMeshesOp : public FVoxelBaseOp
{
public:
	virtual ~FVoxelSolidifyMeshesOp() {}

	// inputs
	TArray<TSharedPtr<const FDynamicMesh3>> Meshes;
	TArray<FTransform> Transforms; // 1:1 with Meshes

	double WindingThreshold = .5;
	double ExtendBounds = 1;
	bool bSolidAtBoundaries = true;
	int SurfaceSearchSteps = 3;

	bool bMakeOffsetSurfaces = false;
	double OffsetThickness = 5;

	void SetTransform(const FTransform& Transform);

	//
	// FDynamicMeshOperator implementation
	// 

	virtual void CalculateResult(FProgressCancel* Progress) override;

};


