// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MeshSpaceDeformerOp.h"



class MODELINGOPERATORS_API FTwistMeshOp : public FMeshSpaceDeformerOp
{
public:
	virtual void CalculateResult(FProgressCancel* Progress) override;

	double TwistDegrees = 180;
	bool bLockBottom = false;
protected:

};