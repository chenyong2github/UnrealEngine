// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/DataLayer/DataLayerHelper.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "Misc/HashBuilder.h"
#include "Algo/Transform.h"

#if WITH_EDITOR
uint32 FDataLayersHelper::ComputeDataLayerID(const TArray<const UDataLayer*>& InDataLayers)
{
	if (InDataLayers.Num())
	{
		TArray<FName> SortedDataLayers;
		Algo::TransformIf(InDataLayers, SortedDataLayers, [](const UDataLayer* Item) { return Item->IsDynamicallyLoaded(); }, [](const UDataLayer* Item) { return Item->GetFName(); });
		SortedDataLayers.Sort([](const FName& A, const FName& B) { return A.FastLess(B); });
		FHashBuilder HashBuilder;
		HashBuilder << SortedDataLayers;
		return HashBuilder.GetHash();
	}
	return FDataLayersHelper::NoDataLayerID;
}
#endif