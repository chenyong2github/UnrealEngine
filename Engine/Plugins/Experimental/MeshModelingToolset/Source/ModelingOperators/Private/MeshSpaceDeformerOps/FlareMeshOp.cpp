// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "ModelingOperators\Public\SpaceDeformerOps\FlareMeshOp.h"



//Some simple non-linear interpolation functions to play with.

template <class T>
T coserp(float percent, const T& value1, const T& value2)
{
	return T(0.5) * (cos(percent*PI) * (value1 - value2) + value1 + value2); 
}

template <class T>
double inverseCoserp(const T& valueBetween, const T& value1, const T& value2)
{
	return acos((T(2) * valueBetween - value1 - value2) / (value1 - value2)) / PI;
}

template <class T>
T sinerp(float percent, const T& value1, const T& value2)
{
	return T(0.5) * (sin(percent * PI) * (value1 - value2) + value1 + value2);   
}

template <class T>
double  inverseSinerp(const T& valueBetween, const T& value1, const T& value2)
{
	return asin((T(2.0) * valueBetween - value1 - value2) / (value1 - value2)) / PI;
}


//Flares along the Z axis
void FFlareMeshOp::CalculateResult(FProgressCancel* Progress)
{
	FMatrix3d Rotate{ FVector3d{0,1,0}, FVector3d{0,0,1}, FVector3d{1,0,0}, false };
	FMatrix3d ToOpSpace = Rotate * ObjectSpaceToOpSpace;

	const FVector3d O = ToOpSpace * AxisOriginObjectSpace;
	const double& Z0 = O.Z;
	const double ZMin = Z0 + LowerBoundsInterval * AxesHalfLengths[2];
	const double ZMax = Z0 + UpperBoundsInterval * AxesHalfLengths[2];

	FMatrix3d FromOpSpace = ToOpSpace.Inverse();
	for (int VertexID : TargetMesh->VertexIndicesItr())
	{
		const FVector3d& OriginalPositionObjectSpace = OriginalPositions[VertexID];

		const FVector3d Pos = ToOpSpace * (OriginalPositionObjectSpace);

		double T = FMath::Clamp(FMath::Abs((Z0 - Pos.Z)) / (ZMax - ZMin) * 0.5, 0.0, 1.0);
		double R = coserp((1.0-T), 1.0, GetModifierValue());

		double X = Pos.X * R;
		double Y = Pos.Y * R;
		double Z = Pos.Z;

		TargetMesh->SetVertex(VertexID, FromOpSpace * (FVector3d{ X,Y,Z }));
	}

}