// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/EvolutionResimCache.h"
#include "Chaos/Collision/CollisionResimCache.h"

namespace Chaos
{
	int32 UseCollisionResimCache = 0;
	FAutoConsoleVariableRef CVarUseCollisionResimCache(TEXT("p.UseCollisionResimCache"),UseCollisionResimCache,TEXT("Whether to skip collision detection during resim"));

	FEvolutionResimCache::~FEvolutionResimCache() = default;
	
	FEvolutionResimCache::FEvolutionResimCache()
	: CollisionResimCache(nullptr)
	{
		if(UseCollisionResimCache)
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
