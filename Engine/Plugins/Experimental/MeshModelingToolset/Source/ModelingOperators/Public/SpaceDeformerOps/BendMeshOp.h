// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MeshSpaceDeformerOp.h"


class MODELINGOPERATORS_API FBendMeshOp : public FMeshSpaceDeformerOp
{
public:
	virtual void CalculateResult(FProgressCancel* Progress) override;

	double BendDegrees = 90;
	bool bLockBottom = false;

protected:
};