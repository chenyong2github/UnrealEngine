// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Util/ProgressCancel.h"
#include "ModelingOperators.h"

#include "BaseOps/VoxelBaseOp.h"



class MODELINGOPERATORS_API FVoxelBlendMeshesOp : public FVoxelBaseOp
{
public:
	virtual ~FVoxelBlendMeshesOp() {}

	// inputs
	TArray<TSharedPtr<const FDynamicMesh3>> Meshes;
	TArray<FTransform> Transforms; // 1:1 with Meshes

	double BlendFalloff = 10;
	double BlendPower = 2;

	bool bSolidifyInput = false;
	bool bRemoveInternalsAfterSolidify = false;
	double OffsetSolidifySurface = 0.0;

	void SetTransform(const FTransform& Transform);

	//
	// FDynamicMeshOperator implementation
	// 

	virtual void CalculateResult(FProgressCancel* Progress) override;

};


