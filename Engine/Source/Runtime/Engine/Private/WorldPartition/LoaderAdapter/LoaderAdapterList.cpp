// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/LoaderAdapter/LoaderAdapterList.h"

#if WITH_EDITOR
FLoaderAdapterList::FLoaderAdapterList(UWorld* InWorld)
	: ILoaderAdapter(InWorld)
{}

void FLoaderAdapterList::ForEachActor(TFunctionRef<void(const FWorldPartitionHandle&)> InOperation) const
{
	for (const FWorldPartitionHandle& Actor : Actors)
	{
		InOperation(Actor);
	}
}
#endif
