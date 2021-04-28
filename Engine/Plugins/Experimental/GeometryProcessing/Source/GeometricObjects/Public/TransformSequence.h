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
	 * @return point P with transform sequence applied
	 */
	FVector3d TransformPosition(FVector3d P) const
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
	FVector3d InverseTransformPosition(FVector3d P) const
	{
		int32 N = Transforms.Num();
		for (int32 k = N - 1; k >= 0; k--)
		{
			P = Transforms[k].InverseTransformPosition(P);
		}
		return P;
	}

	/**
	 * Create an reversed transform sequence such that InverseSeq.Transform(P) is equivalent to Seq.InverseTransform(P).
	 * This is more efficient if applying inverse many times as it saves lots of checking for invalid scaling.
	 * @return inverse transform sequence
	 */
	TTransformSequence3<RealType> Inverse() const
	{
		TTransformSequence3 Inverse;
		int32 N = Transforms.Num();
		Inverse.Transforms.Reserve(N);
		for (int32 k = N - 1; k >= 0; --k)
		{
			Inverse.Transforms.Add(Transforms[k].Inverse());
		}
		return Inverse;
	}


};

typedef TTransformSequence3<float> FTransformSequence3f;
typedef TTransformSequence3<double> FTransformSequence3d;


} // end namespace UE::Geometry
} // end namespace UE