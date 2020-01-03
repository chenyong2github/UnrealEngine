// Copyright Epic Games, Inc. All Rights Reserved.

#include "Math/ControlRigMathLibrary.h"
#include "AHEasing/easing.h"
#include "TwoBoneIK.h"

float FControlRigMathLibrary::AngleBetween(const FVector& A, const FVector& B)
{
	if (A.IsNearlyZero() || B.IsNearlyZero())
	{
		return 0.f;
	}

	return FMath::Acos(FVector::DotProduct(A, B) / (A.Size() * B.Size()));
}

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

void FControlRigMathLibrary::FourPointBezier(const FCRFourPointBezier& Bezier, float T, FVector& OutPosition, FVector& OutTangent)
{
	FourPointBezier(Bezier.A, Bezier.B, Bezier.C, Bezier.D, T, OutPosition, OutTangent);
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
		case EControlRigAnimEasingType::QuadraticEaseIn:
		{
			Value = QuadraticEaseIn(Value);
			break;
		}
		case EControlRigAnimEasingType::QuadraticEaseOut:
		{
			Value = QuadraticEaseOut(Value);
			break;
		}
		case EControlRigAnimEasingType::QuadraticEaseInOut:
		{
			Value = QuadraticEaseInOut(Value);
			break;
		}
		case EControlRigAnimEasingType::CubicEaseIn:
		{
			Value = CubicEaseIn(Value);
			break;
		}
		case EControlRigAnimEasingType::CubicEaseOut:
		{
			Value = CubicEaseOut(Value);
			break;
		}
		case EControlRigAnimEasingType::CubicEaseInOut:
		{
			Value = CubicEaseInOut(Value);
			break;
		}
		case EControlRigAnimEasingType::QuarticEaseIn:
		{
			Value = QuarticEaseIn(Value);
			break;
		}
		case EControlRigAnimEasingType::QuarticEaseOut:
		{
			Value = QuarticEaseOut(Value);
			break;
		}
		case EControlRigAnimEasingType::QuarticEaseInOut:
		{
			Value = QuarticEaseInOut(Value);
			break;
		}
		case EControlRigAnimEasingType::QuinticEaseIn:
		{
			Value = QuinticEaseIn(Value);
			break;
		}
		case EControlRigAnimEasingType::QuinticEaseOut:
		{
			Value = QuinticEaseOut(Value);
			break;
		}
		case EControlRigAnimEasingType::QuinticEaseInOut:
		{
			Value = QuinticEaseInOut(Value);
			break;
		}
		case EControlRigAnimEasingType::SineEaseIn:
		{
			Value = SineEaseIn(Value);
			break;
		}
		case EControlRigAnimEasingType::SineEaseOut:
		{
			Value = SineEaseOut(Value);
			break;
		}
		case EControlRigAnimEasingType::SineEaseInOut:
		{
			Value = SineEaseInOut(Value);
			break;
		}
		case EControlRigAnimEasingType::CircularEaseIn:
		{
			Value = CircularEaseIn(Value);
			break;
		}
		case EControlRigAnimEasingType::CircularEaseOut:
		{
			Value = CircularEaseOut(Value);
			break;
		}
		case EControlRigAnimEasingType::CircularEaseInOut:
		{
			Value = CircularEaseInOut(Value);
			break;
		}
		case EControlRigAnimEasingType::ExponentialEaseIn:
		{
			Value = ExponentialEaseIn(Value);
			break;
		}
		case EControlRigAnimEasingType::ExponentialEaseOut:
		{
			Value = ExponentialEaseOut(Value);
			break;
		}
		case EControlRigAnimEasingType::ExponentialEaseInOut:
		{
			Value = ExponentialEaseInOut(Value);
			break;
		}
		case EControlRigAnimEasingType::ElasticEaseIn:
		{
			Value = ElasticEaseIn(Value);
			break;
		}
		case EControlRigAnimEasingType::ElasticEaseOut:
		{
			Value = ElasticEaseOut(Value);
			break;
		}
		case EControlRigAnimEasingType::ElasticEaseInOut:
		{
			Value = ElasticEaseInOut(Value);
			break;
		}
		case EControlRigAnimEasingType::BackEaseIn:
		{
			Value = BackEaseIn(Value);
			break;
		}
		case EControlRigAnimEasingType::BackEaseOut:
		{
			Value = BackEaseOut(Value);
			break;
		}
		case EControlRigAnimEasingType::BackEaseInOut:
		{
			Value = BackEaseInOut(Value);
			break;
		}
		case EControlRigAnimEasingType::BounceEaseIn:
		{
			Value = BounceEaseIn(Value);
			break;
		}
		case EControlRigAnimEasingType::BounceEaseOut:
		{
			Value = BounceEaseOut(Value);
			break;
		}
		case EControlRigAnimEasingType::BounceEaseInOut:
		{
			Value = BounceEaseInOut(Value);
			break;
		}
	}

	return Value;
}

FTransform FControlRigMathLibrary::LerpTransform(const FTransform& A, const FTransform& B, float T)
{
	FTransform Result = FTransform::Identity;
	Result.SetLocation(FMath::Lerp<FVector>(A.GetLocation(), B.GetLocation(), T));
	Result.SetRotation(FQuat::Slerp(A.GetRotation(), B.GetRotation(), T));
	Result.SetScale3D(FMath::Lerp<FVector>(A.GetScale3D(), B.GetScale3D(), T));
	return Result;
}

void FControlRigMathLibrary::SolveBasicTwoBoneIK(FTransform& BoneA, FTransform& BoneB, FTransform& Effector, const FVector& PoleVector, const FVector& PrimaryAxis, const FVector& SecondaryAxis, float SecondaryAxisWeight, float BoneALength, float BoneBLength, bool bEnableStretch, float StretchStartRatio, float StretchMaxRatio)
{
	FVector RootPos = BoneA.GetLocation();
	FVector ElbowPos = BoneB.GetLocation();
	FVector EffectorPos = Effector.GetLocation();

	AnimationCore::SolveTwoBoneIK(RootPos, ElbowPos, EffectorPos, PoleVector, EffectorPos, ElbowPos, EffectorPos, BoneALength, BoneBLength, bEnableStretch, StretchStartRatio, StretchMaxRatio);

	BoneB.SetLocation(ElbowPos);
	Effector.SetLocation(EffectorPos);

	FVector Axis = BoneA.TransformVectorNoScale(PrimaryAxis);
	FVector Target1 = BoneB.GetLocation() - BoneA.GetLocation();

	if (!Target1.IsNearlyZero() && !Axis.IsNearlyZero())
	{
		Target1 = Target1.GetSafeNormal();
		FQuat Rotation1 = FQuat::FindBetweenNormals(Axis, Target1);
		BoneA.SetRotation((Rotation1 * BoneA.GetRotation()).GetNormalized());

		Axis = BoneA.TransformVectorNoScale(SecondaryAxis);

		if (SecondaryAxisWeight > SMALL_NUMBER)
		{
			FVector Target2 = BoneB.GetLocation() - (Effector.GetLocation() + BoneA.GetLocation()) * 0.5f;
			if (!Target2.IsNearlyZero() && !Axis.IsNearlyZero())
			{
				Target2 = Target2 - FVector::DotProduct(Target2, Target1) * Target1;
				Target2 = Target2.GetSafeNormal();

				FQuat Rotation2 = FQuat::FindBetweenNormals(Axis, Target2);
				if (!FMath::IsNearlyEqual(SecondaryAxisWeight, 1.f))
				{
					FVector RotationAxis = Rotation2.GetRotationAxis();
					float RotationAngle = Rotation2.GetAngle();
					Rotation2 = FQuat(RotationAxis, RotationAngle * FMath::Clamp<float>(SecondaryAxisWeight, 0.f, 1.f));
				}
				BoneA.SetRotation((Rotation2 * BoneA.GetRotation()).GetNormalized());
			}
		}
	}

	Axis = BoneB.TransformVectorNoScale(PrimaryAxis);
	Target1 = Effector.GetLocation() - BoneB.GetLocation();
	if (!Target1.IsNearlyZero() && !Axis.IsNearlyZero())
	{
		Target1 = Target1.GetSafeNormal();
		FQuat Rotation1 = FQuat::FindBetweenNormals(Axis, Target1);
		BoneB.SetRotation((Rotation1 * BoneB.GetRotation()).GetNormalized());

		if (SecondaryAxisWeight > SMALL_NUMBER)
		{
			Axis = BoneB.TransformVectorNoScale(SecondaryAxis);
			FVector Target2 = BoneB.GetLocation() - (Effector.GetLocation() + BoneA.GetLocation()) * 0.5f;
			if (!Target2.IsNearlyZero() && !Axis.IsNearlyZero())
			{
				Target2 = Target2 - FVector::DotProduct(Target2, Target1) * Target1;
				Target2 = Target2.GetSafeNormal();

				FQuat Rotation2 = FQuat::FindBetweenNormals(Axis, Target2);
				if (!FMath::IsNearlyEqual(SecondaryAxisWeight, 1.f))
				{
					FVector RotationAxis = Rotation2.GetRotationAxis();
					float RotationAngle = Rotation2.GetAngle();
					Rotation2 = FQuat(RotationAxis, RotationAngle * FMath::Clamp<float>(SecondaryAxisWeight, 0.f, 1.f));
				}
				BoneB.SetRotation((Rotation2 * BoneB.GetRotation()).GetNormalized());
			}
		}
	}
}

FVector FControlRigMathLibrary::ClampSpatially(const FVector& Value, EAxis::Type Axis, EControlRigClampSpatialMode::Type Type, float Minimum, float Maximum, FTransform Space)
{
	FVector Local = Space.InverseTransformPosition(Value);

	switch (Type)
	{
		case EControlRigClampSpatialMode::Plane:
		{
			switch (Axis)
			{
				case EAxis::X:
				{
					Local.X = FMath::Clamp<float>(Local.X, Minimum, Maximum);
					break;
				}
				case EAxis::Y:
				{
					Local.Y = FMath::Clamp<float>(Local.Y, Minimum, Maximum);
					break;
				}
				default:
				{
					Local.Z = FMath::Clamp<float>(Local.Z, Minimum, Maximum);
					break;
				}
			}
			break;
		}
		case EControlRigClampSpatialMode::Cylinder:
		{
			switch (Axis)
			{
				case EAxis::X:
				{
					FVector OnPlane = Local * FVector(0.f, 1.f, 1.f);
					if (!OnPlane.IsNearlyZero())
					{
						float Length = OnPlane.Size();
						OnPlane = OnPlane * FMath::Clamp<float>(Length, Minimum, Maximum) / Length;
						Local.Y = OnPlane.Y;
						Local.Z = OnPlane.Z;
					}
					break;
				}
				case EAxis::Y:
				{
					FVector OnPlane = Local * FVector(1.f, 0.f, 1.f);
					if (!OnPlane.IsNearlyZero())
					{
						float Length = OnPlane.Size();
						OnPlane = OnPlane * FMath::Clamp<float>(Length, Minimum, Maximum) / Length;
						Local.X = OnPlane.X;
						Local.Z = OnPlane.Z;
					}
					break;
				}
				default:
				{
					FVector OnPlane = Local * FVector(1.f, 1.f, 0.f);
					if (!OnPlane.IsNearlyZero())
					{
						float Length = OnPlane.Size();
						OnPlane = OnPlane * FMath::Clamp<float>(Length, Minimum, Maximum) / Length;
						Local.X = OnPlane.X;
						Local.Y = OnPlane.Y;
					}
					break;
				}
			}
			break;
		}
		default:
		case EControlRigClampSpatialMode::Sphere:
		{
			if (!Local.IsNearlyZero())
			{
				float Length = Local.Size();
				Local = Local * FMath::Clamp<float>(Length, Minimum, Maximum) / Length;
			}
			break;
		}

	}

	return Space.TransformPosition(Local);
}
