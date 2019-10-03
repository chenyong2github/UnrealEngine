// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Util/ProgressCancel.h"
#include "ModelingOperators.h"





class MODELINGOPERATORS_API FPlaneCutOp : public FDynamicMeshOperator
{
public:
	virtual ~FPlaneCutOp() {}

	// inputs
	FVector LocalPlaneOrigin, LocalPlaneNormal;
	bool bFillCutHole, bDiscardAttributes, bFillSpans;
	TSharedPtr<FDynamicMesh3> OriginalMesh;

	void SetTransform(const FTransform& Transform);

	//
	// FDynamicMeshOperator implementation
	// 

	virtual void CalculateResult(FProgressCancel* Progress) override;
};


