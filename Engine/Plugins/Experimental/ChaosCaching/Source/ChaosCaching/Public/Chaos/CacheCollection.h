// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CacheCollection.generated.h"

class UChaosCache;

UCLASS(Experimental)
class CHAOSCACHING_API UChaosCacheCollection : public UObject
{
	GENERATED_BODY()
public:

	UChaosCache* FindCache(const FName& CacheName) const;
	UChaosCache* FindOrAddCache(const FName& CacheName);

	void FlushAllCacheWrites();

	const TArray<UChaosCache*> GetCaches() const;

	UPROPERTY(EditAnywhere, Instanced, Category="Caching", meta=(EditFixedOrder))
	TArray<UChaosCache*> Caches;
};