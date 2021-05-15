// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TransformTypes.h"

namespace UE
{
namespace Geometry
{


/**
 * TTransformSequence3 represents a sequence of 3D transforms. 
 */
template<typename RealType>
class TTransformSequence3
{
protected:
	TArray<TTransform3<RealType>, TInlineAllocator<2>> Transforms;

public:

	/**
	 * Add Transform to the end of the sequence, ie Seq(P) becomes NewTransform * Seq(P)
	 */
	void Append(const TTransform3<RealType>& Transform)
	{
		Transforms.Add(Transform);
	}

	/**
	 * Add Transform to the end of the sequence, ie Seq(P) becomes NewTransform * Seq(P)
	 */
	void Append(const FTransform& Transform)
	{
		Transforms.Add( TTransform3<RealType>(Transform) );
	}

	/**
	 * @return number of transforms in the sequence
	 */
	int32 Num() const { return Transforms.Num(); }

	/**
	 * @return transforms in the sequence
	 */
	const TArray<TTransform3<RealType>,TInlineAllocator<2>>& GetTransforms() const { return Transforms; }


	/**
	 * @return true if any transform in the sequence has nonuniform scaling
	 */
	bool HasNonUniformScale(RealType Tolerance = TMathUtil<RealType>::ZeroTolerance) const
	{
		for (const TTransform3<RealType>& Transform : Transforms)
		{
			if (Transform.HasNonUniformScale(Tolerance))
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * @return point P with transform sequence applied
	 */
	FVector3<RealType> TransformPosition(UE::Math::TVector<RealType> P) const
	{
		for (const TTransform3<RealType>& Transform : Transforms)
		{
			P = Transform.TransformPosition(P);
		}
		return P;
	}

	/**
	 * @return point P with inverse transform sequence applied
	 */
	FVector3<RealType> InverseTransformPosition(UE::Math::TVector<RealType> P) const
	{
		int32 N = Transforms.Num();
		for (int32 k = N - 1; k >= 0; k--)
		{
			P = Transforms[k].InverseTransformPosition(P);
		}
		return P;
	}

	/**
	 * @return Vector V with transform sequence applied
	 */
	FVector3<RealType> TransformVector(UE::Math::TVector<RealType> V) const
	{
		for (const TTransform3<RealType>& Transform : Transforms)
		{
			V = Transform.TransformVector(V);
		}
		return V;
	}


	/**
	 * @return Normal with transform sequence applied
	 */
	FVector3<RealType> TransformNormal(UE::Math::TVector<RealType> Normal) const
	{
		for (const TTransform3<RealType>& Transform : Transforms)
		{
			Normal = Transform.TransformNormal(Normal);
		}
		return Normal;
	}


};

typedef TTransformSequence3<float> FTransformSequence3f;
typedef TTransformSequence3<double> FTransformSequence3d;


} // end namespace UE::Geometry
} // end namespace UE