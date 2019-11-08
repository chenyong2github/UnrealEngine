// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "ModelingOperators\Public\SpaceDeformerOps\FlareMeshOp.h"
#include "DynamicMeshAttributeSet.h"



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



	if (ResultMesh->HasAttributes())
	{
		// Fix the normals first if they exist.

		FDynamicMeshNormalOverlay* Normals = ResultMesh->Attributes()->PrimaryNormals();
		for (int ElID : Normals->ElementIndicesItr())
		{
			// get the vertex
			auto VertexID = Normals->GetParentVertex(ElID);
			const FVector3d& SrcPos = TargetMesh->GetVertex(VertexID);

			FVector3f SrcNormalF = Normals->GetElement(ElID);
			FVector3d SrcNormal; 
			SrcNormal[0] = SrcNormalF[0]; SrcNormal[1] = SrcNormalF[1]; SrcNormal[2] = SrcNormalF[2];


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

			FVector3d RotatedNormal(0, 0, 0);
			{
				// Rotate normal to gizmo space.
				for (int i = 0; i < 3; ++i)
				{
					for (int j = 0; j < 3; ++j)
					{
						RotatedNormal[i] += GizmoToObject.M[j][i] * SrcNormal[j];
					}
				}
			}


			const double T = FMath::Clamp((GizmoPos4[2] - ZMin) / (ZMax - ZMin), 0.0, 1.0);

			const double R = 1. + FMath::Sin(PI * T) * (ModifierPercent / 100.f);
			
			// transform normal.  Do this before changing GizmoPos

			{

				double DR = FMath::Cos(PI * T) * (PI / (ZMax - ZMin)) *  (ModifierPercent / 100.f);
				if (GizmoPos4[2] > ZMax || GizmoPos4[2] < ZMin)  DR = 0.f;
			

				FVector3d DstNormal(0., 0., 0.);
				DstNormal[0] = R * RotatedNormal[0];
				DstNormal[1] = R * RotatedNormal[1];
				DstNormal[2] = -R * DR * (RotatedNormal[0] * GizmoPos4[0] + RotatedNormal[1] * GizmoPos4[1]) + R * R * RotatedNormal[2];

				// rotate back to mesh space.
				RotatedNormal = FVector3d(0, 0, 0);
				for (int i = 0; i < 3; ++i)
				{
					for (int j = 0; j < 3; ++j)
					{
						RotatedNormal[i] += ObjectToGizmo.M[j][i] * DstNormal[j];
					}
				}

			}

			FVector3f RotatedNoramlF;
			RotatedNoramlF[0] = RotatedNormal[0]; RotatedNoramlF[1] = RotatedNormal[1]; RotatedNoramlF[2] = RotatedNormal[2];
			Normals->SetElement(ElID, RotatedNoramlF);
		}
	}

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