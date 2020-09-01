// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/CacheCollection.h"
#include "Chaos/ChaosCache.h"
#include "Algo/Find.h"
#include "Async/ParallelFor.h"

UChaosCache* UChaosCacheCollection::FindCache(const FName& CacheName) const
{
	UChaosCache* const* ExistingCache = Algo::FindByPredicate(Caches, [&CacheName](const UChaosCache* Test)
	{
		return Test->GetFName() == CacheName;
	});

	return ExistingCache ? *ExistingCache : nullptr;
}

UChaosCache* UChaosCacheCollection::FindOrAddCache(const FName& CacheName)
{
	FName        FinalName   = CacheName;
	UChaosCache* ResultCache = nullptr;

	if(FinalName != NAME_None)
	{
		UChaosCache** ExistingCache = Algo::FindByPredicate(Caches, [&FinalName](const UChaosCache* Test)
		{
			return Test->GetFName() == FinalName;
		});

		ResultCache = ExistingCache ? *ExistingCache : nullptr;
	}

	if(!ResultCache)
	{
		// Final check for unique name, or no name - generate one from the base name
		// (GetPlainNameString so we don't get increasing strings of appended numbers)
		if(StaticFindObject(UChaosCache::StaticClass(), this, *FinalName.ToString()) || FinalName == NAME_None)
		{
			FinalName = MakeUniqueObjectName(this, UChaosCache::StaticClass(), *FinalName.GetPlainNameString());
		}

		ResultCache = NewObject<UChaosCache>(this, FinalName, RF_Transactional);

		Caches.Add(ResultCache);
	}

	return ResultCache;
}

void UChaosCacheCollection::FlushAllCacheWrites()
{
	ParallelFor(Caches.Num(), [this](int32 InIndex)
	{
		Caches[InIndex]->FlushPendingFrames();
	});
}

const TArray<UChaosCache*> UChaosCacheCollection::GetCaches() const
{
	return Caches;
}
