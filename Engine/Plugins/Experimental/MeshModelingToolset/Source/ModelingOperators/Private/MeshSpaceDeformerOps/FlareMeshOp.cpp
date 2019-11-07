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

	float Det = ObjectToGizmo.Determinant();

	// Check if the transform is nearly singular
	// this could happen if the scale on the object to world transform has a very small component.
	if (FMath::Abs(Det) < 1.e-4)
	{
		return;
	}

	FMatrix GizmoToObject = ObjectToGizmo.Inverse();

	const double ZMin = -LowerBoundsInterval * AxesHalfLength;
	const double ZMax =  UpperBoundsInterval * AxesHalfLength;

	for (int VertexID : TargetMesh->VertexIndicesItr())
	{
		
		const FVector3d SrcPos = TargetMesh->GetVertex(VertexID);

		const double SrcPos4[4] = { SrcPos[0], SrcPos[1], SrcPos[2], 1.0 };

		// Position in gizmo space
		double GizmoPos4[4] = { 0., 0., 0., 0. };
		for (int i = 0; i < 4; ++i)
		{
			for (int j = 0; j < 4; ++j)
			{
				GizmoPos4[i] += ObjectToGizmo.M[i][j] * SrcPos4[j];
			}
		}

		
		// Parameterize curve between ZMin and ZMax to go between 0, 1
		double T = FMath::Clamp( (GizmoPos4[2]- ZMin) / (ZMax - ZMin), 0.0, 1.0);

		double R = 1. + FMath::Sin(PI * T) * (ModifierPercent / 100.f);

		// 2d scale x,y values.
		GizmoPos4[0] *= R;
		GizmoPos4[1] *= R;


		double DstPos4[4] = { 0., 0., 0., 0. };
		for (int i = 0; i < 4; ++i)
		{
			for (int j = 0; j < 4; ++j)
			{
				DstPos4[i] += GizmoToObject.M[i][j] * GizmoPos4[j];
			}
		}

		// set the position
		TargetMesh->SetVertex(VertexID, FVector3d(DstPos4[0], DstPos4[1], DstPos4[2]));
	}

}