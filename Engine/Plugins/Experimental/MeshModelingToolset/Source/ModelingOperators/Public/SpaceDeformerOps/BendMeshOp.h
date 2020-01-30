// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MeshSpaceDeformerOp.h"


class MODELINGOPERATORS_API FBendMeshOp : public FMeshSpaceDeformerOp
{
public:
	virtual ~FBendMeshOp() {}
	FBendMeshOp() : FMeshSpaceDeformerOp(0.0, 180.1) {};
	virtual void CalculateResult(FProgressCancel* Progress) override;

protected:
};