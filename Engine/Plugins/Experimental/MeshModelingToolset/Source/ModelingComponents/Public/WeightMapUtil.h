// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VectorTypes.h"
#include "IndexTypes.h"
#include "BoxTypes.h"

struct FMeshDescription;

/**
 * FIndexedWeightMap stores an array of values, the intention is that these are "weights" on indices,
 * for example per-vertex weights. 
 */
class FIndexedWeightMap
{
public:
	float DefaultValue;
	TArray<float> Values;

	float GetValue(int32 Index) const
	{
		return Values[Index];
	}

	float GetInterpValue(const FIndex3i& Indices, const FVector3d& BaryCoords) const
	{
		return (float)((double)Values[Indices.A]*BaryCoords.X
			+ (double)Values[Indices.B]*BaryCoords.Y
			+ (double)Values[Indices.C]*BaryCoords.Z);
	}

	void InvertWeightMap(FInterval1f Range = FInterval1f(0.0f,1.0f))
	{
		int32 Num = Values.Num();
		for (int32 k = 0; k < Num; ++k)
		{
			Values[k] = Range.Clamp( (Range.Max - (Values[k] - Range.Min)) );
		}
	}
};


namespace UE
{
	namespace WeightMaps
	{

		/**
		 * Find the set of per-vertex weight map attributes on a MeshDescription
		 */
		MODELINGCOMPONENTS_API void FindVertexWeightMaps(const FMeshDescription* Mesh, TArray<FName>& PropertyNamesOut);

		/**
		 * Extract a per-vertex weight map from a MeshDescription
		 * If the attribute with the given name is not found, a WeightMap initialized with the default value is returned
		 * @return false if weight map was not found
		 */
		MODELINGCOMPONENTS_API bool GetVertexWeightMap(const FMeshDescription* Mesh, FName AttributeName, FIndexedWeightMap& WeightMap, float DefaultValue = 1.0f);

	}
}