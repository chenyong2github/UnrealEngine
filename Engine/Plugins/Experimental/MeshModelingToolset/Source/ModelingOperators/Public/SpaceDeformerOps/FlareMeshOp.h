// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MeshSpaceDeformerOp.h"



class MODELINGOPERATORS_API FFlareMeshOp : public FMeshSpaceDeformerOp
{
public:
	virtual ~FFlareMeshOp() {}
	FFlareMeshOp() : FMeshSpaceDeformerOp(0.1, 2.0) {};
	virtual void CalculateResult(FProgressCancel* Progress) override;


protected:

};