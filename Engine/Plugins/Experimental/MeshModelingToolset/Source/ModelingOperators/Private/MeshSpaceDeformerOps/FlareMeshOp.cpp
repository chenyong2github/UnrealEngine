// Copyright Epic Games, Inc. All Rights Reserved.
#include "SpaceDeformerOps/FlareMeshOp.h"

#include "Async/ParallelFor.h"
#include "DynamicMeshAttributeSet.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

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
	FMeshSpaceDeformerOp::CalculateResult(Progress);

	if (!OriginalMesh || (Progress && Progress->Cancelled()))
	{
		return;
	}

	float Det = ObjectToGizmo.Determinant();

	// Check if the transform is nearly singular
	// this could happen if the scale on the object to world transform has a very small component.
	if (FMath::Abs(Det) < 1.e-4)
	{
		return;
	}

	FMatrix GizmoToObject = ObjectToGizmo.Inverse();

	const double ZMin = LowerBoundsInterval;
	const double ZMax =  UpperBoundsInterval;

	if (ResultMesh->HasAttributes())
	{
		// Fix the normals first if they exist.

		FDynamicMeshNormalOverlay* Normals = ResultMesh->Attributes()->PrimaryNormals();
		ParallelFor(Normals->MaxElementID(), [this, Normals, &GizmoToObject, ZMin, ZMax](int32 ElID)
		{
			if (!Normals->IsElement(ElID))
			{
				return;
			}

			// get the vertex
			auto VertexID = Normals->GetParentVertex(ElID);
			const FVector3d& SrcPos = ResultMesh->GetVertex(VertexID);

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

			double Rx = bSmoothEnds ? 1. + (FMath::Cos(2 * PI * T - PI) + 1) * (FlarePercentX / 200.f) // Shift cos curve up 1, right PI, scale down by 2, go from 0 to 2PI
				: 1. + FMath::Sin(PI * T) * (FlarePercentX / 100.f);
			double Ry = bSmoothEnds ? 1. + (FMath::Cos(2 * PI * T - PI) + 1) * (FlarePercentY / 200.f) // Shift cos curve up 1, right PI, scale down by 2, go from 0 to 2PI
				: 1. + FMath::Sin(PI * T) * (FlarePercentY / 100.f);
			
			// transform normal.  Do this before changing GizmoPos
			// To get the normal, note that the positions are transformed like this:
			// X = Rx * x
			// Y = Ry * y
			// Z = z
			// The jacobian is:
			// Rx  0  x*DRx
			// 0  Ry  y*DRy
			// 0   0   1
			// Where DRx is dRx/dz and DRy is dRy/dz
			// Then take the transpose of the inverse of the Jacobian and multiply by the determinant (or don't, since we don't
			// care about the length, but it's cleaner if you do).
			{
				double DRx = bSmoothEnds ? -FMath::Sin(2 * PI * T - PI) * (2 * PI / (ZMax - ZMin)) * (FlarePercentX / 200.f)
					: FMath::Cos(PI * T) * (PI / (ZMax - ZMin)) * (FlarePercentX / 100.f);
				double DRy = bSmoothEnds ? -FMath::Sin(2 * PI * T - PI) * (2 * PI / (ZMax - ZMin)) * (FlarePercentY / 200.f)
					: FMath::Cos(PI * T) * (PI / (ZMax - ZMin)) *  (FlarePercentY / 100.f);

				if (GizmoPos4[2] > ZMax || GizmoPos4[2] < ZMin)
				{
					DRx = DRy = 0.f;
				}
			
				FVector3d DstNormal(0., 0., 0.);
				DstNormal[0] = Ry * RotatedNormal[0];
				DstNormal[1] = Rx * RotatedNormal[1];
				DstNormal[2] = -Ry * DRx * GizmoPos4[0] * RotatedNormal[0] - Rx * DRy * GizmoPos4[1] * RotatedNormal[1] + Rx * Ry * RotatedNormal[2];

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

			Normals->SetElement(ElID, FVector3f(RotatedNormal.Normalized()));
		});
	}

	if (Progress && Progress->Cancelled())
	{
		return;
	}

	ParallelFor(ResultMesh->MaxVertexID(), [this, &GizmoToObject, ZMin, ZMax](int32 VertexID)
	{
		if (!ResultMesh->IsVertex(VertexID))
		{
			return;
		}
		
		const FVector3d SrcPos = ResultMesh->GetVertex(VertexID);

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

		double Rx = bSmoothEnds ? 1. + (FMath::Cos(2 * PI * T - PI) + 1) * (FlarePercentX / 200.f) // Shift cos curve up 1, right PI, scale down by 2, go from 0 to 2PI
			: 1. + FMath::Sin(PI * T) * (FlarePercentX / 100.f);
		double Ry = bSmoothEnds ? 1. + (FMath::Cos(2 * PI * T - PI) + 1) * (FlarePercentY / 200.f) // Shift cos curve up 1, right PI, scale down by 2, go from 0 to 2PI
			: 1. + FMath::Sin(PI * T) * (FlarePercentY / 100.f);

		// 2d scale x,y values.
		GizmoPos4[0] *= Rx;
		GizmoPos4[1] *= Ry;


		double DstPos4[4] = { 0., 0., 0., 0. };
		for (int i = 0; i < 4; ++i)
		{
			for (int j = 0; j < 4; ++j)
			{
				DstPos4[i] += GizmoToObject.M[i][j] * GizmoPos4[j];
			}
		}

		// set the position
		ResultMesh->SetVertex(VertexID, FVector3d(DstPos4[0], DstPos4[1], DstPos4[2]));
	});

}