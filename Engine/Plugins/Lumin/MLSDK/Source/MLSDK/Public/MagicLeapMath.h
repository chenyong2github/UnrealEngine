// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/KismetMathLibrary.h"
#include "CoreMinimal.h"
#include "Lumin/CAPIShims/LuminAPITypes.h"

namespace MagicLeap
{
	static const float kIdentityMatColMajor[] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
#if WITH_MLSDK
	static const MLTransform kIdentityTransform = { { { { 0, 0, 0, 1 } } },{ { { 0, 0, 0 } } } };

	FORCEINLINE FVector ToFVector(const MLVec3f& InVec3f, float WorldToMetersScale)
	{
		return FVector(-InVec3f.xyz.z * WorldToMetersScale, InVec3f.xyz.x * WorldToMetersScale, InVec3f.xyz.y * WorldToMetersScale);
	}

	FORCEINLINE FVector ToFVectorNoScale(const MLVec3f& InVec3f)
	{
		return FVector(-InVec3f.xyz.z, InVec3f.xyz.x, InVec3f.xyz.y);
	}

	FORCEINLINE FVector ToFVectorExtents(const MLVec3f& InVec3f, float WorldToMetersScale)
	{
		// No sign change for FVector::X
		return FVector(InVec3f.xyz.z * WorldToMetersScale, InVec3f.xyz.x * WorldToMetersScale, InVec3f.xyz.y * WorldToMetersScale);
	}

	FORCEINLINE MLVec3f ToMLVector(const FVector& InVector, float WorldToMetersScale)
	{
		float InverseScale = 1.0f / WorldToMetersScale;
		MLVec3f vec;
		vec.xyz.x = InVector.Y * InverseScale;
		vec.xyz.y = InVector.Z * InverseScale;
		vec.xyz.z = -InVector.X * InverseScale;
		return vec;
	}

	FORCEINLINE MLVec3f ToMLVectorNoScale(const FVector& InVector)
	{
		return ToMLVector(InVector, 1.0f);
	}

	FORCEINLINE MLVec3f ToMLVectorExtents(const FVector& InVector, float WorldToMetersScale)
	{
		float InverseScale = 1.0f / WorldToMetersScale;
		MLVec3f vec;
		vec.xyz.x = InVector.Y * InverseScale;
		vec.xyz.y = InVector.Z * InverseScale;
		// No sign change for MLVec3f::z
		vec.xyz.z = InVector.X * InverseScale;
		return vec;
	}

	FORCEINLINE FQuat ToFQuat(const MLQuaternionf& InQuat)
	{
		return FQuat(-InQuat.z, InQuat.x, InQuat.y, -InQuat.w);
	}

	FORCEINLINE MLQuaternionf ToMLQuat(const FQuat& InQuat)
	{
		MLQuaternionf quat;
		quat.x = InQuat.Y;
		quat.y = InQuat.Z;
		quat.z = -InQuat.X;
		quat.w = -InQuat.W;
		return quat;
	}

	FORCEINLINE FTransform ToFTransform(const MLTransform& InTransform, float WorldToMetersScale)
	{
		return FTransform(ToFQuat(InTransform.rotation), ToFVector(InTransform.position, WorldToMetersScale), FVector(1.0f, 1.0f, 1.0f));
	}

	FORCEINLINE MLTransform ToMLTransform(const FTransform& InTransform, float WorldToMetersScale)
	{
		MLTransform transform = { { { { 0, 0, 0, 1 } } }, { { { 0, 0, 0 } } } };
		transform.position = ToMLVector(InTransform.GetLocation(), WorldToMetersScale);
		transform.rotation = ToMLQuat(InTransform.GetRotation());
		return transform;
	}

	// Simple col major to row major conversion
	FORCEINLINE FMatrix ToFMatrix(const MLMat4f& InMat4f)
	{
		return FMatrix(
			FPlane(InMat4f.matrix_colmajor[0], InMat4f.matrix_colmajor[1], InMat4f.matrix_colmajor[2], InMat4f.matrix_colmajor[3]),
			FPlane(InMat4f.matrix_colmajor[4], InMat4f.matrix_colmajor[5], InMat4f.matrix_colmajor[6], InMat4f.matrix_colmajor[7]),
			FPlane(InMat4f.matrix_colmajor[8], InMat4f.matrix_colmajor[9], InMat4f.matrix_colmajor[10], InMat4f.matrix_colmajor[11]),
			FPlane(InMat4f.matrix_colmajor[12], InMat4f.matrix_colmajor[13], InMat4f.matrix_colmajor[14], InMat4f.matrix_colmajor[15])
		);
	}

	FORCEINLINE FMatrix ToFMatrix2(const MLMat4f& InMat4f, const float WorldToMetersScale)
	{
		return FMatrix(
			FPlane(-InMat4f.matrix_colmajor[10], InMat4f.matrix_colmajor[8], InMat4f.matrix_colmajor[9], 0.0f),
			FPlane(-InMat4f.matrix_colmajor[2], InMat4f.matrix_colmajor[0], InMat4f.matrix_colmajor[1], 0.0f),
			FPlane(-InMat4f.matrix_colmajor[6], InMat4f.matrix_colmajor[4], InMat4f.matrix_colmajor[5], 0.0f),
			FPlane(-InMat4f.matrix_colmajor[14] * WorldToMetersScale, InMat4f.matrix_colmajor[12] * WorldToMetersScale, InMat4f.matrix_colmajor[13] * WorldToMetersScale, 1.0f)
		);
	}

	// Convert from MLGraphics Projection Model to Unreal's Reverse Infinite Z Projection Model 	
	FORCEINLINE FMatrix ToUEProjectionMatrix(const MLMat4f& InProjMat, float NearClipPlane)
	{
		/*
			* matrix_colmajor indices *

			| 00  04  08  12 |
			| 01  05  09  13 |
			| 02  06  10  14 |
			| 03  07  11  15 |

			* ml_projection_colmajor format *
			* A, B depends on requested Z format *

			| X  0  0   0 |
			| 0  Y  0   0 |
			| 0  0  A   B |
			| 0  0  -1  0 |

			* ue_projection_rowmajor format *
			* A = 0, B = N (NearClip) *

			* Unreal is left handed and MLGraphics is right handed so ue_projection_rowmajor[2] = -1.0 x ue_projection_rowmajor[2] *
			* which makes ue_projection_rowmajor[2][3] = 1.0f instead of -1.0f. *
			* Unreal expects the projection matrix to be in centimeters and uses it for various purposes *
			* such as bounding volume calculations for lights in the shadow algorithm. *
			* We're overwriting the near value to match the units of unreal here instead of using the units of the MLSDK. *

			| X  0  0  0 |
			| 0  Y  0  0 |
			| 0  0  0  1 |
			| 0  0  N  0 |
		*/

		return FMatrix(
			FPlane(InProjMat.matrix_colmajor[0], InProjMat.matrix_colmajor[1], InProjMat.matrix_colmajor[2], InProjMat.matrix_colmajor[3]),
			FPlane(InProjMat.matrix_colmajor[4], InProjMat.matrix_colmajor[5], InProjMat.matrix_colmajor[6], InProjMat.matrix_colmajor[7]),
			FPlane(-InProjMat.matrix_colmajor[8], -InProjMat.matrix_colmajor[9],			0.0f		   ,			1.0f			 ),
			FPlane(InProjMat.matrix_colmajor[12], InProjMat.matrix_colmajor[13],		NearClipPlane	   , InProjMat.matrix_colmajor[15])
		);
	}

	FORCEINLINE FVector ToFVector(const MLMat4f& InMatrix, float WorldToMetersScale)
	{
		MLVec3f MLPosition;
		MLPosition.xyz.x = InMatrix.matrix_colmajor[12];
		MLPosition.xyz.y = InMatrix.matrix_colmajor[13];
		MLPosition.xyz.z = InMatrix.matrix_colmajor[14];
		return ToFVector(MLPosition, WorldToMetersScale);
	}

	FORCEINLINE FQuat ToFQuat(const MLMat4f& InMatrix)
	{
		const float Trace = InMatrix.matrix_colmajor[0] + InMatrix.matrix_colmajor[5] + InMatrix.matrix_colmajor[10];

		MLQuaternionf Output;
		if (Trace > 0)
		{
			const float S = FMath::Sqrt(Trace + 1.0) * 2;
			Output.w = S * 0.25;
			Output.x = (InMatrix.matrix_colmajor[6] - InMatrix.matrix_colmajor[9]) / S;
			Output.y = (InMatrix.matrix_colmajor[8] - InMatrix.matrix_colmajor[2]) / S;
			Output.z = (InMatrix.matrix_colmajor[1] - InMatrix.matrix_colmajor[4]) / S;
		}
		else if (InMatrix.matrix_colmajor[0] > InMatrix.matrix_colmajor[5] && InMatrix.matrix_colmajor[0] > InMatrix.matrix_colmajor[10])
		{
			const float S = FMath::Sqrt(InMatrix.matrix_colmajor[0] - InMatrix.matrix_colmajor[5] - InMatrix.matrix_colmajor[10] + 1.0) * 2;
			Output.x = S * 0.25;
			Output.w = (InMatrix.matrix_colmajor[6] - InMatrix.matrix_colmajor[9]) / S;
			Output.y = (InMatrix.matrix_colmajor[1] - InMatrix.matrix_colmajor[4]) / S;
			Output.z = (InMatrix.matrix_colmajor[8] - InMatrix.matrix_colmajor[2]) / S;
		}
		else if (InMatrix.matrix_colmajor[5] > InMatrix.matrix_colmajor[10])
		{
			const float S = FMath::Sqrt(InMatrix.matrix_colmajor[5] - InMatrix.matrix_colmajor[0] - InMatrix.matrix_colmajor[10] + 1.0) * 2;
			Output.y = S * 0.25;
			Output.w = (InMatrix.matrix_colmajor[8] - InMatrix.matrix_colmajor[2]) / S;
			Output.x = (InMatrix.matrix_colmajor[1] - InMatrix.matrix_colmajor[4]) / S;
			Output.z = (InMatrix.matrix_colmajor[6] - InMatrix.matrix_colmajor[9]) / S;
		}
		else
		{
			const float S = FMath::Sqrt(InMatrix.matrix_colmajor[10] - InMatrix.matrix_colmajor[5] - InMatrix.matrix_colmajor[0] + 1.0) * 2;
			Output.z = S * 0.25;
			Output.w = (InMatrix.matrix_colmajor[1] - InMatrix.matrix_colmajor[4]) / S;
			Output.x = (InMatrix.matrix_colmajor[8] - InMatrix.matrix_colmajor[2]) / S;
			Output.y = (InMatrix.matrix_colmajor[6] - InMatrix.matrix_colmajor[9]) / S;
		}

		return ToFQuat(Output);
	}

	FORCEINLINE MLVec3f ExtractScale3D(const MLMat4f& InMatrix)
	{
		MLVec3f MLScale;
		MLScale.x = FMath::Sqrt(
			FMath::Square(InMatrix.matrix_colmajor[0])
			+ FMath::Square(InMatrix.matrix_colmajor[1])
			+ FMath::Square(InMatrix.matrix_colmajor[2]));
		MLScale.y = FMath::Sqrt(
			FMath::Square(InMatrix.matrix_colmajor[4])
			+ FMath::Square(InMatrix.matrix_colmajor[5])
			+ FMath::Square(InMatrix.matrix_colmajor[6]));
		MLScale.z = FMath::Sqrt(
			FMath::Square(InMatrix.matrix_colmajor[8])
			+ FMath::Square(InMatrix.matrix_colmajor[9])
			+ FMath::Square(InMatrix.matrix_colmajor[10]));
		return MLScale;
	}

	FORCEINLINE MLMat4f ToMLMat4f(const FVector& InPosition, const FQuat& InOrientation, const FVector& InScale3D, const float WorldToMetersScale)
	{
		FMatrix OutMatrix = FTransform(InOrientation, InPosition, InScale3D).ToMatrixWithScale();
		return MLMat4f {
			{
				OutMatrix.M[1][1], OutMatrix.M[1][2], -OutMatrix.M[1][0], 0.0f,
				OutMatrix.M[2][1], OutMatrix.M[2][2], -OutMatrix.M[2][0], 0.0f,
				OutMatrix.M[0][1], OutMatrix.M[0][2], -OutMatrix.M[0][0], 0.0f,
				OutMatrix.M[3][1] / WorldToMetersScale, OutMatrix.M[3][2] / WorldToMetersScale, -OutMatrix.M[3][0] / WorldToMetersScale, 1.0f
			}
		};
	}
#endif //WITH_MLSDK

	FORCEINLINE FQuat ToUERotator(const FQuat& InMLRotation)
	{
		return FQuat(UKismetMathLibrary::GetRightVector(InMLRotation.Rotator()), PI) * InMLRotation;
	}
}
