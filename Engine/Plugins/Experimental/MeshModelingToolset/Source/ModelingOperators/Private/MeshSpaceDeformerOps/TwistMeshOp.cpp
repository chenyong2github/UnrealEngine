// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ModelingOperators\Public\SpaceDeformerOps\TwistMeshOp.h"

//Twists along the Z axis
void FTwistMeshOp::CalculateResult(FProgressCancel* Progress)
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


	const double DegreesToRadians = 0.017453292519943295769236907684886; // Pi / 180
	const double ThetaRadians = DegreesToRadians * GetModifierValue();

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

		double T = FMath::Clamp( (GizmoPos4[2] - ZMin) / ( ZMax - ZMin ), 0.0, 1.0) - 0.5;
		double Theta = ThetaRadians * T;

		const double C0 = FMath::Cos(Theta);
		const double S0 = FMath::Sin(Theta);
		
		// apply 2d rotation.
		const double X =  C0 * GizmoPos4[0]  + S0 * GizmoPos4[1];
		const double Y = -S0 * GizmoPos4[0]  + C0 * GizmoPos4[1];
		
		GizmoPos4[0] = X;
		GizmoPos4[1] = Y;

		// Position in Obj Space
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