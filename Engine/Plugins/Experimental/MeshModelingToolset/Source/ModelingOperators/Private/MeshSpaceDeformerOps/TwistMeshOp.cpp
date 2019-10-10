// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ModelingOperators\Public\SpaceDeformerOps\TwistMeshOp.h"

//Twists along the Z axis
void FTwistMeshOp::CalculateResult(FProgressCancel* Progress)
{
	FMatrix3d Rotate{FVector3d{0,1,0}, FVector3d{0,0,1}, FVector3d{1,0,0}, false};
	FMatrix3d ToOpSpace = Rotate * ObjectSpaceToOpSpace;

	const FVector3d O = ToOpSpace * AxisOriginObjectSpace;
	const double& Z0 = O.Z;
	const double ZMin = Z0 + LowerBoundsInterval * AxesHalfLengths[2];
	const double ZMax = Z0 + UpperBoundsInterval * AxesHalfLengths[2];

	const double ToRadians = 0.017453292519943295;
	const double ThetaRadians = ToRadians * GetModifierValue();

	FMatrix3d FromOpSpace = ToOpSpace.Inverse();
	for (int VertexID : TargetMesh->VertexIndicesItr())
	{
		const FVector3d& OriginalPositionObjectSpace = OriginalPositions[VertexID];

		const FVector3d Pos = ToOpSpace * (OriginalPositionObjectSpace);

		double T = FMath::Clamp(( ZMax - Pos.Z ) / ( ZMax - ZMin ), 0.0, 1.0);
		double Theta = ThetaRadians * T;

		const double C0 = FMath::Cos(Theta);
		const double S0 = FMath::Sin(Theta);

		double X = Pos.X * C0 + Pos.Y * S0;
		double Y = Pos.X * (-S0) + Pos.Y * C0;
		double Z = Pos.Z;

		TargetMesh->SetVertex(VertexID, FromOpSpace * FVector3d{ X,Y,Z });
	}
}