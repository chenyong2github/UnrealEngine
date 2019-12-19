// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SpaceDeformerOps\MeshSpaceDeformerOp.h"
#include "DynamicMesh3.h"

void FMeshSpaceDeformerOp::CalculateResult(FProgressCancel* Progress)
{
}

void FMeshSpaceDeformerOp::CopySource(const FDynamicMesh3& MeshIn, const FTransform& XForm)
{
	ResultMesh = MakeUnique < FDynamicMesh3>(MeshIn);
	ResultTransform = FTransform3d(XForm);

	TargetMesh = ResultMesh.Get();

}
