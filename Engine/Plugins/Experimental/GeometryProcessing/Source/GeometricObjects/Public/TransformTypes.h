// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VectorTypes.h"
#include "Quaternion.h"

/**
 * TTransform3 is a double/float templated version of standard UE FTransform.
 */
template<typename RealType>
class TTransform3
{
protected:
	TQuaternion<RealType> Rotation;
	FVector3<RealType> Translation;
	FVector3<RealType> Scale3D;

public:

	TTransform3()
	{
		Rotation = TQuaternion<RealType>::Identity();
		Translation = FVector3<RealType>::Zero();
		Scale3D = FVector3<RealType>::One();
	}

	TTransform3(const TQuaternion<RealType>& RotationIn, const FVector3<RealType>& TranslationIn, const FVector3<RealType>& ScaleIn)
	{
		Rotation = RotationIn;
		Translation = TranslationIn;
		Scale3D = ScaleIn;
	}

	explicit TTransform3(const TQuaternion<RealType>& RotationIn, const FVector3<RealType>& TranslationIn)
	{
		Rotation = RotationIn;
		Translation = TranslationIn;
		Scale3D = FVector3<RealType>::One();
	}

	explicit TTransform3(const FVector3<RealType>& TranslationIn)
	{
		Rotation = TQuaternion<RealType>::Identity();
		Translation = TranslationIn;
		Scale3D = FVector3<RealType>::One();
	}

	explicit TTransform3(const FTransform& Transform)
	{
		Rotation = TQuaternion<RealType>(Transform.GetRotation());
		Translation = FVector3<RealType>(Transform.GetTranslation());
		Scale3D = FVector3<RealType>(Transform.GetScale3D());
	}

	/**
	 * @return identity transform, IE no rotation, zero origin, unit scale
	 */
	static TTransform3<RealType> Identity()
	{
		return TTransform3<RealType>(TQuaternion<RealType>::Identity(), FVector3<RealType>::Zero(), FVector3<RealType>::One());
	}

	/**
	 * @return this transform converted to FTransform 
	 */
	explicit operator FTransform() const
	{
		return FTransform((FQuat)Rotation, (FVector)Translation, (FVector)Scale3D);
	}


	/**
	 * @return input point with QST transformation applied, ie QST(P) = Rotate(Scale*P) + Translate
	 */
	FVector3<RealType> TransformPosition(const FVector3<RealType>& P) const
	{
		//Transform using QST is following
		//QST(P) = Q.Rotate(S*P) + T where Q = quaternion, S = scale, T = translation
		return Rotation * (Scale3D*P) + Translation;
	}

	/**
	 * @return input vector with QS transformation applied, ie QS(V) = Rotate(Scale*V)
	 */
	FVector3<RealType> TransformVector(const FVector3<RealType>& V) const
	{
		return Rotation * (Scale3D*V);
	}

	/**
	 * @return input vector with Q transformation applied, ie Q(V) = Rotate(V)
	 */
	FVector3<RealType> TransformVectorNoScale(const FVector3<RealType>& V) const
	{
		return Rotation * V;
	}

	/**
	 * @return input vector with inverse-QST transformation applied, ie QSTinv(P) = InverseScale(InverseRotate(P - Translate))
	 */
	FVector3<RealType> InverseTransformPosition(const FVector3<RealType> &P) const
	{
		return GetSafeScaleReciprocal(Scale3D) * Rotation.InverseMultiply(P - Translation);
	}

	/**
	 * @return input vector with inverse-QS transformation applied, ie QSinv(V) = InverseScale(InverseRotate(V))
	 */
	FVector3<RealType> InverseTransformVector(const FVector3<RealType> &V) const
	{
		return GetSafeScaleReciprocal(Scale3D) * Rotation.InverseMultiply(V);
	}


	/**
	 * @return input vector with inverse-Q transformation applied, ie Qinv(V) = InverseRotate(V)
	 */
	FVector3<RealType> InverseTransformVectorNoScale(const FVector3<RealType> &V) const
	{
		return Rotation.InverseMultiply(V);
	}

	

	static FVector3<RealType> GetSafeScaleReciprocal(const FVector3<RealType>& InScale, RealType Tolerance = TMathUtil<RealType>::ZeroTolerance)
	{
		FVector3<RealType> SafeReciprocalScale;
		if (TMathUtil<RealType>::Abs(InScale.X) <= Tolerance)
		{
			SafeReciprocalScale.X = (RealType)0;
		}
		else
		{
			SafeReciprocalScale.X = (RealType)1 / InScale.X;
		}

		if (TMathUtil<RealType>::Abs(InScale.Y) <= Tolerance)
		{
			SafeReciprocalScale.Y = (RealType)0;
		}
		else
		{
			SafeReciprocalScale.Y = (RealType)1 / InScale.Y;
		}

		if (TMathUtil<RealType>::Abs(InScale.Z) <= Tolerance)
		{
			SafeReciprocalScale.Z = (RealType)0;
		}
		else
		{
			SafeReciprocalScale.Z = (RealType)1 / InScale.Z;
		}

		return SafeReciprocalScale;
	}



};
typedef TTransform3<float> FTransform3f;
typedef TTransform3<double> FTransform3d;
