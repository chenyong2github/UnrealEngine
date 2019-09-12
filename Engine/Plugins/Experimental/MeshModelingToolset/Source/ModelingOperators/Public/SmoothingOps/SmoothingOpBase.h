// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Util/ProgressCancel.h"
#include "ModelingOperators.h"
#include "VectorTypes.h"


class FDynamicMesh3;

class MODELINGOPERATORS_API FSmoothingOpBase : public FDynamicMeshOperator
{
public:
	FSmoothingOpBase(const FDynamicMesh3* Mesh, float Speed, int32 Iterations);

	virtual ~FSmoothingOpBase() override {}

	// set ability on protected transform.
	void SetTransform(FTransform3d& XForm);

	// base class overrides this.  Results in updated ResultMesh.
	virtual void CalculateResult(FProgressCancel* Progress) override = 0;

	// copy the PositionBuffer locations back to the ResultMesh and recompute normal if it exists.
	void UpdateResultMesh();

protected:
	float SmoothSpeed;
	int32 SmoothIterations;

	TArray<FVector3d> PositionBuffer;

};