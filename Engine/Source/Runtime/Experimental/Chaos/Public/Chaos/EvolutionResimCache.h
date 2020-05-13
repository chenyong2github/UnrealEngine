// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ResimCacheBase.h"
#include "Templates/UniquePtr.h"

namespace Chaos
{
	class FCollisionResimCache;

	class FEvolutionResimCache : public IResimCacheBase
	{
	public:
		FEvolutionResimCache(bool bUseCollisionResimCache);
		virtual ~FEvolutionResimCache();
		FCollisionResimCache* GetCollisionResimCache(){ return CollisionResimCache.Get(); }

		void ResetCache();

	private:
		TUniquePtr<FCollisionResimCache> CollisionResimCache;
	};

} // namespace Chaos
