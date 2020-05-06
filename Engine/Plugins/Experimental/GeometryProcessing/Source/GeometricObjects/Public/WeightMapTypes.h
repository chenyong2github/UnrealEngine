// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VectorTypes.h"
#include "IndexTypes.h"


/**
 * FIndexedWeightMap stores an array of values, the intention is that these are "weights" on indices,
 * for example per-vertex weights.
 */
template<typename RealType>
class TIndexedWeightMap
{
public:
	RealType DefaultValue;
	TArray<RealType> Values;

	RealType GetValue(int32 Index) const
	{
		return Values[Index];
	}

	RealType GetInterpValue(const FIndex3i& Indices, const FVector3d& BaryCoords) const
	{
		return (RealType)((double)Values[Indices.A] * BaryCoords.X
			+ (double)Values[Indices.B] * BaryCoords.Y
			+ (double)Values[Indices.C] * BaryCoords.Z);
	}

	void InvertWeightMap(TInterval1<RealType> Range = TInterval1<RealType>((RealType)0, (RealType)1.0))
	{
		int32 Num = Values.Num();
		for (int32 k = 0; k < Num; ++k)
		{
			Values[k] = Range.Clamp((Range.Max - (Values[k] - Range.Min)));
		}
	}
};

typedef TIndexedWeightMap<float> FIndexedWeightMap;
typedef TIndexedWeightMap<float> FIndexedWeightMap1f;
typedef TIndexedWeightMap<double> FIndexedWeightMap1d;
