// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/EvolutionResimCache.h"
#include "Chaos/Collision/CollisionResimCache.h"

namespace Chaos
{
	FEvolutionResimCache::~FEvolutionResimCache() = default;
	
	FEvolutionResimCache::FEvolutionResimCache(bool bUseCollisionResimCache)
	: CollisionResimCache(nullptr)
	{
		if(bUseCollisionResimCache)
		{
			CollisionResimCache = MakeUnique<FCollisionResimCache>();
		}
	}

	void FEvolutionResimCache::ResetCache()
	{
		if(CollisionResimCache)
		{
			CollisionResimCache->ResetCache();
		}
	}
}
