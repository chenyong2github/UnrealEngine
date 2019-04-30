// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Math/ControlRigMathLibrary.h"

FQuat FControlRigMathLibrary::QuatFromEuler(const FVector& XYZAnglesInDegrees, EControlRigRotationOrder RotationOrder)
{
	float X = FMath::DegreesToRadians(XYZAnglesInDegrees.X);
	float Y = FMath::DegreesToRadians(XYZAnglesInDegrees.Y);
	float Z = FMath::DegreesToRadians(XYZAnglesInDegrees.Z);

	float CosX = FMath::Cos( X * 0.5f );
	float CosY = FMath::Cos( Y * 0.5f );
	float CosZ = FMath::Cos( Z * 0.5f );

	float SinX = FMath::Sin( X * 0.5f );
	float SinY = FMath::Sin( Y * 0.5f );
	float SinZ = FMath::Sin( Z * 0.5f );

	if ( RotationOrder == EControlRigRotationOrder::XYZ )
	{
		return FQuat(SinX * CosY * CosZ - CosX * SinY * SinZ, 
					 CosX * SinY * CosZ + SinX * CosY * SinZ,
					 CosX * CosY * SinZ - SinX * SinY * CosZ,
					 CosX * CosY * CosZ + SinX * SinY * SinZ);

	}
	else if ( RotationOrder == EControlRigRotationOrder::XZY )
	{
		return FQuat(SinX * CosY * CosZ + CosX * SinY * SinZ, 
					 CosX * SinY * CosZ + SinX * CosY * SinZ,
					 CosX * CosY * SinZ - SinX * SinY * CosZ,
					 CosX * CosY * CosZ - SinX * SinY * SinZ);

	}
	else if ( RotationOrder == EControlRigRotationOrder::YXZ )
	{
		return FQuat(SinX * CosY * CosZ - CosX * SinY * SinZ, 
					 CosX * SinY * CosZ + SinX * CosY * SinZ,
					 CosX * CosY * SinZ + SinX * SinY * CosZ,
					 CosX * CosY * CosZ - SinX * SinY * SinZ);

	}
	else if ( RotationOrder == EControlRigRotationOrder::YZX )
	{
		return FQuat(SinX * CosY * CosZ - CosX * SinY * SinZ, 
					 CosX * SinY * CosZ - SinX * CosY * SinZ,
					 CosX * CosY * SinZ + SinX * SinY * CosZ,
					 CosX * CosY * CosZ + SinX * SinY * SinZ);
	}
	else if ( RotationOrder == EControlRigRotationOrder::ZXY )
	{
		return FQuat(SinX * CosY * CosZ + CosX * SinY * SinZ, 
					 CosX * SinY * CosZ - SinX * CosY * SinZ,
					 CosX * CosY * SinZ - SinX * SinY * CosZ,
					 CosX * CosY * CosZ + SinX * SinY * SinZ);

	}
	else if ( RotationOrder == EControlRigRotationOrder::ZYX )
	{
		return FQuat(SinX * CosY * CosZ + CosX * SinY * SinZ, 
					 CosX * SinY * CosZ - SinX * CosY * SinZ,
					 CosX * CosY * SinZ + SinX * SinY * CosZ,
					 CosX * CosY * CosZ - SinX * SinY * SinZ);

	}

	// should not happen
	return FQuat::Identity;
}

FVector FControlRigMathLibrary::EulerFromQuat(const FQuat& Rotation, EControlRigRotationOrder RotationOrder)
{
	float X = Rotation.X;
	float Y = Rotation.Y;
	float Z = Rotation.Z;
	float W = Rotation.W;
	float X2 = X * 2.f;
	float Y2 = Y * 2.f;
	float Z2 = Z * 2.f;
	float XX2 = X * X2;
	float XY2 = X * Y2;
	float XZ2 = X * Z2;
	float YX2 = Y * X2;
	float YY2 = Y * Y2;
	float YZ2 = Y * Z2;
	float ZX2 = Z * X2;
	float ZY2 = Z * Y2;
	float ZZ2 = Z * Z2;
	float WX2 = W * X2;
	float WY2 = W * Y2;
	float WZ2 = W * Z2;

	FVector AxisX, AxisY, AxisZ;
	AxisX.X = (1.f - (YY2 + ZZ2));
	AxisY.X = (XY2 + WZ2);
	AxisZ.X = (XZ2 - WY2);
	AxisX.Y = (XY2 - WZ2);
	AxisY.Y = (1.f - (XX2 + ZZ2));
	AxisZ.Y = (YZ2 + WX2);
	AxisX.Z = (XZ2 + WY2);
	AxisY.Z = (YZ2 - WX2);
	AxisZ.Z = (1.f - (XX2 + YY2));

	FVector Result = FVector::ZeroVector;

	if ( RotationOrder == EControlRigRotationOrder::XYZ )
	{
		Result.Y = FMath::Asin( - FMath::Clamp<float>( AxisZ.X, -1.f, 1.f ) );

		if ( FMath::Abs( AxisZ.X ) < 1.f - SMALL_NUMBER )
		{
			Result.X = FMath::Atan2( AxisZ.Y, AxisZ.Z );
			Result.Z = FMath::Atan2( AxisY.X, AxisX.X );
		}
		else
		{
			Result.X = 0.f;
			Result.Z = FMath::Atan2( -AxisX.Y, AxisY.Y );
		}
	}
	else if ( RotationOrder == EControlRigRotationOrder::XZY )
	{

		Result.Z = FMath::Asin( FMath::Clamp<float>( AxisY.X, -1.f, 1.f ) );

		if ( FMath::Abs( AxisY.X ) < 1.f - SMALL_NUMBER )
		{
			Result.X = FMath::Atan2( -AxisY.Z, AxisY.Y );
			Result.Y = FMath::Atan2( -AxisZ.X, AxisX.X );
		}
		else
		{
			Result.X = 0.f;
			Result.Y = FMath::Atan2( AxisX.Z, AxisZ.Z );
		}
	}
	else if ( RotationOrder == EControlRigRotationOrder::YXZ )
	{
		Result.X = FMath::Asin( FMath::Clamp<float>( AxisZ.Y, -1.f, 1.f ) );

		if ( FMath::Abs( AxisZ.Y ) < 1.f - SMALL_NUMBER )
		{
			Result.Y = FMath::Atan2( -AxisZ.X, AxisZ.Z );
			Result.Z = FMath::Atan2( -AxisX.Y, AxisY.Y );
		}
		else
		{
			Result.Y = 0.f;
			Result.Z = FMath::Atan2( AxisY.X, AxisX.X );
		}
	}
	else if ( RotationOrder == EControlRigRotationOrder::YZX )
	{
		Result.Z = FMath::Asin( - FMath::Clamp<float>( AxisX.Y, -1.f, 1.f ) );

		if ( FMath::Abs( AxisX.Y ) < 1.f - SMALL_NUMBER )
		{
			Result.X = FMath::Atan2( AxisZ.Y, AxisY.Y );
			Result.Y = FMath::Atan2( AxisX.Z, AxisX.X );
		}
		else
		{
			Result.X = FMath::Atan2( -AxisY.Z, AxisZ.Z );
			Result.Y = 0.f;
		}
	}
	else if ( RotationOrder == EControlRigRotationOrder::ZXY )
	{
		Result.X = FMath::Asin( - FMath::Clamp<float>( AxisY.Z, -1.f, 1.f ) );

		if ( FMath::Abs( AxisY.Z ) < 1.f - SMALL_NUMBER )
		{
			Result.Y = FMath::Atan2( AxisX.Z, AxisZ.Z );
			Result.Z = FMath::Atan2( AxisY.X, AxisY.Y );
		}
		else
		{
			Result.Y = FMath::Atan2( -AxisZ.X, AxisX.X );
			Result.Z = 0.f;
		}
	}
	else if ( RotationOrder == EControlRigRotationOrder::ZYX )
	{
		Result.Y = FMath::Asin( FMath::Clamp<float>( AxisX.Z, -1.f, 1.f ) );

		if ( FMath::Abs( AxisX.Z ) < 1.f - SMALL_NUMBER )
		{
			Result.X = FMath::Atan2( -AxisY.Z, AxisZ.Z );
			Result.Z = FMath::Atan2( -AxisX.Y, AxisX.X );
		}
		else
		{
			Result.X = FMath::Atan2( AxisZ.Y, AxisY.Y );
			Result.Z = 0.f;
		}
	}

	return Result * 180.f / PI;
}

void FControlRigMathLibrary::FourPointBezier(const FVector& A, const FVector& B, const FVector& C, const FVector& D, float T, FVector& OutPosition, FVector& OutTangent)
{
	const FVector AB = FMath::Lerp<FVector>(A, B, T);
	const FVector BC = FMath::Lerp<FVector>(B, C, T);
	const FVector CD = FMath::Lerp<FVector>(C, D, T);
	const FVector ABBC = FMath::Lerp<FVector>(AB, BC, T);
	const FVector BCCD = FMath::Lerp<FVector>(BC, CD, T);
	OutPosition = FMath::Lerp<FVector>(ABBC, BCCD, T);
	OutTangent = (BCCD - ABBC).GetSafeNormal();
}

float FControlRigMathLibrary::EaseFloat(float Value, EControlRigAnimEasingType Type)
{
	switch(Type)
	{
		case EControlRigAnimEasingType::Linear:
		{
			break;
		}
		case EControlRigAnimEasingType::QuadraticIn:
		{
			Value = Value * Value;
			break;
		}
		case EControlRigAnimEasingType::QuadraticOut:
		{
			Value = -(Value * (Value - 2.f));
			break;
		}
		case EControlRigAnimEasingType::QuadraticInOut:
		{
			if (Value < 0.5f)
			{
				Value = 2.f * Value * Value;
			}
			else
			{
				Value = (-2.f * Value * Value) + (4.f * Value) - 1.f;
			}
			break;
		}
		case EControlRigAnimEasingType::CubicIn:
		{
			Value = Value * Value * Value;
			break;
		}
		case EControlRigAnimEasingType::CubicOut:
		{
			Value = Value - 1.f;
			Value = Value * Value * Value + 1.f;
			break;
		}
		case EControlRigAnimEasingType::CubicInOut:
		{
			if (Value < 0.5f)
			{
				Value = 4.f * Value * Value * Value;
			}
			else
			{
				Value = 2.f * Value - 2.f;
				Value = 0.5f * Value * Value * Value + 1.f;
			}
			break;
		}
		case EControlRigAnimEasingType::Sinusoidal:
		{
			Value = (FMath::Sin(Value * PI - HALF_PI) + 1.f) * 0.5f;
			break;
		}
	}

	return Value;
}