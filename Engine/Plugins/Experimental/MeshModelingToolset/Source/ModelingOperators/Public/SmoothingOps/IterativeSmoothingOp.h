// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SmoothingOpBase.h"
#include "CoreMinimal.h"
#include "VectorTypes.h"

class MODELINGOPERATORS_API  FIterativeSmoothingOp : public FSmoothingOpBase
{
public:
	FIterativeSmoothingOp(const FDynamicMesh3* Mesh, float Speed, int32 Iterations);

	~FIterativeSmoothingOp() override {};

	// Apply smoothing. results in an updated ResultMesh
	void CalculateResult(FProgressCancel* Progress) override;

private:

	// Does the actual interative smoothign. 
	void Smooth();
	
private:
	TArray<FVector3d> SmoothedBuffer;
};