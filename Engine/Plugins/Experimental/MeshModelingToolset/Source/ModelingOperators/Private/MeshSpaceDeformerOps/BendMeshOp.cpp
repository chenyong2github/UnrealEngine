// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "ModelingOperators\Public\SpaceDeformerOps\BendMeshOp.h"
#include "ProfilingDebugging/ScopedTimers.h"


//Bends along the Y-axis
void FBendMeshOp::CalculateResult(FProgressCancel* Progress)
{

	//FString DebugLogString = FString::Printf(TEXT(" Bend timer "));
	//FScopedDurationTimeLogger Timer(DebugLogString);
	const double Curvature = GetModifierValue();
	const double ToRadians = 0.017453292519943295;
	const double ThetaRadians = ToRadians * Curvature;
	const FVector3d O = ObjectSpaceToOpSpace * AxisOriginObjectSpace;
	const double& Y0 = O.Y;
	double YMin = Y0 + LowerBoundsInterval * AxesHalfLengths[2];
	double YMax = Y0 + UpperBoundsInterval * AxesHalfLengths[2];
	const double K = ThetaRadians / AxesHalfLengths[2];
	const double Ik = 1.0 / K;
	for (int VertexID : TargetMesh->VertexIndicesItr())
	{
		const FVector3d& OriginalPositionObjectSpace = OriginalPositions[VertexID];
		if (Curvature == 0.0)
		{
			TargetMesh->SetVertex(VertexID, OriginalPositionObjectSpace);
			continue;
		}
		const FVector3d Pos = ObjectSpaceToOpSpace * (OriginalPositionObjectSpace);

		const double YHat = FMath::Clamp(Pos.Y, YMin, YMax);

		const double Theta = K * (YHat - Y0);

		const double S0 = FMath::Sin(Theta);
		const double C0 = FMath::Cos(Theta);

		const double ZP = Pos.Z - Ik;

		const double X = Pos.X;
		double Y = -S0 * (ZP) + Y0;
		double Z =  C0 * (ZP) + Ik;

		if (Pos.Y > YMax)
		{
			const double YDiff = Pos.Y - YMax;
			Y += C0 * YDiff;
			Z += S0 * YDiff;
		}
		else if (Pos.Y < YMin)
		{
			const double YDiff = Pos.Y - YMin;
			Y += C0 * YDiff;
			Z += S0 * YDiff;
		}
		TargetMesh->SetVertex(VertexID, OpSpaceToObjectSpace * FVector3d{ X,Y,Z });
	}

}