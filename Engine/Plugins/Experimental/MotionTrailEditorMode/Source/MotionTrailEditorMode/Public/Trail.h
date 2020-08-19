// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/Guid.h"
#include "Containers/Map.h"
#include "Containers/Array.h"
#include "Math/Range.h"
#include "Math/Color.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "GameFramework/Actor.h"

#include "TrajectoryCache.h"

class FTrajectoryDrawInfo;
class FInteractiveTrailTool;
struct FTrailHierarchyNode;

enum class ETrailCacheState
{
	UpToDate,
	Stale,
	Dead
};

class FTrail
{
public:

	FTrail()
		: bForceEvaluateNextTick(true)
	{}

	virtual ~FTrail() {}

	// Public interface methods
	struct FSceneContext
	{
		FGuid YourNode;
		const class FTrailEvaluateTimes& EvalTimes;
		class FTrailHierarchy* TrailHierarchy;
		TMap<FGuid, ETrailCacheState> ParentCacheStates;
	};

	virtual ETrailCacheState UpdateTrail(const FSceneContext& InSceneContext) = 0;
	virtual FTrajectoryCache* GetTrajectoryTransforms() = 0;

	// Optionally implemented methods
	virtual FTrajectoryDrawInfo* GetDrawInfo() { return nullptr; }
	virtual TMap<FString, FInteractiveTrailTool*> GetTools() { return TMap<FString, FInteractiveTrailTool*>(); }
	virtual TRange<double> GetEffectiveRange() const { return TRange<double>::Empty(); }

	void ForceEvaluateNextTick() { bForceEvaluateNextTick = true; }

protected:
	bool bForceEvaluateNextTick;
};

class FRootTrail : public FTrail
{
public:
	FRootTrail()
		: FTrail()
		, TrajectoryCache(MakeUnique<FArrayTrajectoryCache>())
	{}

	virtual ETrailCacheState UpdateTrail(const FSceneContext& InSceneContext) override;
	virtual FTrajectoryCache* GetTrajectoryTransforms() override { return TrajectoryCache.Get(); }

private:
	TUniquePtr<FArrayTrajectoryCache> TrajectoryCache;
};

class FConstantComponentTrail : public FTrail
{
public:

	FConstantComponentTrail(TWeakObjectPtr<class USceneComponent> InWeakComponent)
		: FTrail()
		, CachedEffectiveRange(TRange<double>::Empty())
		, WeakComponent(InWeakComponent)
		, LastLocalTransform(InWeakComponent->GetRelativeTransform())
		, TrajectoryCache(MakeUnique<FArrayTrajectoryCache>(0.01, TRange<double>::Empty()))
	{}

	virtual ETrailCacheState UpdateTrail(const FSceneContext& InSceneContext) override;
	virtual FTrajectoryCache* GetTrajectoryTransforms() override { return TrajectoryCache.Get(); }
	virtual TRange<double> GetEffectiveRange() const override { return CachedEffectiveRange; }

private:
	TRange<double> CachedEffectiveRange;
	TWeakObjectPtr<USceneComponent> WeakComponent;
	FTransform LastLocalTransform;

	TUniquePtr<FArrayTrajectoryCache> TrajectoryCache;
};