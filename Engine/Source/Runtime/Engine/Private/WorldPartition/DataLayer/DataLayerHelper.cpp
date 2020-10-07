// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/DataLayer/DataLayerHelper.h"
#include "Misc/HashBuilder.h"

uint32 FDataLayersHelper::ComputeDataLayerID(const TArray<FName>& InDataLayers)
{
	if (InDataLayers.Num())
	{
		TArray<FName> SortedDataLayers = InDataLayers;
		SortedDataLayers.Sort([](const FName& A, const FName& B) { return A.FastLess(B); });
		FHashBuilder HashBuilder;
		HashBuilder << SortedDataLayers;
		return HashBuilder.GetHash();
	}
	return FDataLayersHelper::NoDataLayerID;
}