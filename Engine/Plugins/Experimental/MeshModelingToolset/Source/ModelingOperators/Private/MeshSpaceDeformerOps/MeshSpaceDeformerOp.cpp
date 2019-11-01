// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SpaceDeformerOps\MeshSpaceDeformerOp.h"
#include "DynamicMesh3.h"

void FMeshSpaceDeformerOp::CalculateResult(FProgressCancel* Progress)
{
}

void FMeshSpaceDeformerOp::UpdateMesh(FDynamicMesh3* TargetMeshComponent)
{
	TargetMesh = TargetMeshComponent;

	OriginalPositions.SetNumUninitialized(TargetMesh->MaxVertexID());
	for (int32 VertexID : TargetMesh->VertexIndicesItr())
	{
		OriginalPositions[VertexID] = TargetMesh->GetVertex(VertexID);
	}
}

void FMeshSpaceDeformerOp::UpdateAxisData(const FMatrix3d & ObjectSpaceToOpSpaceTransform, const FVector3d & ObjectSpaceOrigin, const FVector3d & AxesHalfExtents, double LowerBounds, double UpperBounds, double Modifier)
{
	ObjectSpaceToOpSpace = ObjectSpaceToOpSpaceTransform;  // Rotation matrix to rotate from object space to the space expected by the operator
	OpSpaceToObjectSpace = ObjectSpaceToOpSpace.Inverse(); // Pre-computation for use in the operator
	AxisOriginObjectSpace = ObjectSpaceOrigin;			   // The location of the handle's origin, transformed into the object space of the target mesh
	AxesHalfLengths = AxesHalfExtents;					   // Each element of this vector is a scalar corresponding to the distance from the centroid to the farthest vertex in that respective direction
 	ModifierPercent = Modifier;							   // Input from the tool, as each operator will have a min/max value i.e. the angle of curvature, twist or scale. This is the percent used to interpolate between the min/max value
	LowerBoundsInterval = LowerBounds;					   // The interval [0.0-1.0] representing the range of effect in space this operator will have
	UpperBoundsInterval = UpperBounds;				
}
