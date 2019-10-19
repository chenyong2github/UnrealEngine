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
	 * @return Rotation portion of Transform, as Quaternion
	 */
	const TQuaternion<RealType>& GetRotation() const 
	{ 
		return Rotation; 
	}

	/** 
	 * Set Rotation portion of Transform 
	 */
	void SetRotation(const TQuaternion<RealType>& RotationIn)
	{
		Rotation = RotationIn;
	}

	/**
	 * @return Translation portion of transform
	 */
	const FVector3<RealType>& GetTranslation() const
	{
		return Translation;
	}

	/**
	 * set Translation portion of transform
	 */
	void SetTranslation(const FVector3<RealType>& TranslationIn)
	{
		Translation = TranslationIn;
	}

	/**
	 * @return Scale portion of transform
	 */
	const FVector3<RealType>& GetScale() const
	{
		return Scale3D;
	}

	/**
	 * set Scale portion of transform
	 */
	void SetScale(const FVector3<RealType>& ScaleIn)
	{
		Scale3D = ScaleIn;
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
	 * Surface Normals are special, their transform is Rotate( Normalize( (1/Scale) * Normal) ) ).
	 * However 1/Scale requires special handling in case any component is near-zero.
	 * @return input surface normal with transform applied.
	 */
	FVector3<RealType> TransformNormal(const FVector3<RealType>& Normal) const
	{
		// transform normal by a safe inverse scale + normalize, and a standard rotation
		const FVector3<RealType>& S = Scale3D;
		RealType DetSign = FMathd::SignNonZero(S.X * S.Y * S.Z); // we only need to multiply by the sign of the determinant, rather than divide by it, since we normalize later anyway
		FVector3<RealType> SafeInvS(S.Y*S.Z*DetSign, S.X*S.Z*DetSign, S.X*S.Y*DetSign);
		return TransformVectorNoScale( (SafeInvS*Normal).Normalized() );
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


	/**
	 * Surface Normals are special, their inverse transform is InverseRotate( Normalize(Scale * Normal) ) )
	 * @return input surface normal with inverse transform applied.
	 */
	FVector3<RealType> InverseTransformNormal(const FVector3<RealType>& Normal) const
	{
		return InverseTransformVectorNoScale( (Scale3D*Normal).Normalized() );
	}



	/**
	 * Clamp all scale components to a minimum value. Sign of scale components is preserved.
	 * This is used to remove uninvertible zero/near-zero scaling.
	 */
	void ClampMinimumScale(RealType MinimumScale = TMathUtil<RealType>::ZeroTolerance)
	{
		for (int j = 0; j < 3; ++j)
		{
			RealType Value = Scale3D[j];
			if (TMathUtil<RealType>::Abs(Value) < MinimumScale)
			{
				Value = MinimumScale * TMathUtil<RealType>::SignNonZero(Value);
				Scale3D[j] = Value;
			}
		}
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
