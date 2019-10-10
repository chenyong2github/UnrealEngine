// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SmoothingOpBase.h"
#include "CoreMinimal.h"


class MODELINGOPERATORS_API  FCotanSmoothingOp : public FSmoothingOpBase
{
public:
	FCotanSmoothingOp(const FDynamicMesh3* Mesh, float Speed, int32 Iterations);

	~FCotanSmoothingOp() override {};

	void CalculateResult(FProgressCancel* Progress) override;

private:
	// Compute the smoothed result by using Cotan Biharmonic
	void Smooth();	
};