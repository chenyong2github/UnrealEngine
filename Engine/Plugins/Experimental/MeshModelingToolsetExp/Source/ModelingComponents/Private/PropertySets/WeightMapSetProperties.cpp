// Copyright Epic Games, Inc. All Rights Reserved.

#include "PropertySets/WeightMapSetProperties.h"
#include "WeightMapUtil.h"


void UWeightMapSetProperties::InitializeWeightMaps(const TArray<FName>& WeightMapNames)
{
	// populate weight maps list
	WeightMapsList.Add(TEXT("None"));
	for (FName Name : WeightMapNames)
	{
		WeightMapsList.Add(Name.ToString());
	}
	if (WeightMapNames.Contains(WeightMap) == false)		// discard restored value if it doesn't apply
	{
		WeightMap = FName(WeightMapsList[0]);
	}
}


void UWeightMapSetProperties::InitializeFromMesh(const FMeshDescription* Mesh)
{
	TArray<FName> TargetWeightMaps;
	UE::WeightMaps::FindVertexWeightMaps(Mesh, TargetWeightMaps);
	InitializeWeightMaps(TargetWeightMaps);
}

bool UWeightMapSetProperties::HasSelectedWeightMap() const
{
	return WeightMap != FName(WeightMapsList[0]);
}


TArray<FString> UWeightMapSetProperties::GetWeightMapsFunc()
{
	return WeightMapsList;
}