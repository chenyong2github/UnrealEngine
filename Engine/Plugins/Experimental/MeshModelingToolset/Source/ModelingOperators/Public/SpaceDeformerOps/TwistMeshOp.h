// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MeshSpaceDeformerOp.h"



class MODELINGOPERATORS_API FTwistMeshOp : public FMeshSpaceDeformerOp
{
public:
	virtual ~FTwistMeshOp() {}
	FTwistMeshOp() : FMeshSpaceDeformerOp(0.0, 180.0) {}

	virtual void CalculateResult(FProgressCancel* Progress) override;


protected:

};