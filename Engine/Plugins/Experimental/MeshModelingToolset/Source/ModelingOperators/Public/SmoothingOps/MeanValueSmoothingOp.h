// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SmoothingOpBase.h"
#include "CoreMinimal.h"

class MODELINGOPERATORS_API  FMeanValueSmoothingOp : public FSmoothingOpBase
{
public:
	FMeanValueSmoothingOp(const FDynamicMesh3* Mesh, float Speed, int32 Iterations); 
	
	~FMeanValueSmoothingOp() override {};

	void CalculateResult(FProgressCancel* Progress) override;

private:
	// computed the smoothed result using mean value biharmonic operator
	void Smooth();
	
};