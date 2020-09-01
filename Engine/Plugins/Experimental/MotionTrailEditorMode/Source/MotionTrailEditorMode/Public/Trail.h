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
#include "TrajectoryDrawInfo.h"

namespace UE
{
namespace MotionTrailEditor
{

class FInteractiveTrailTool;
struct FTrailHierarchyNode;

enum class ETrailCacheState : uint8
{
	UpToDate = 2,
	Stale = 1,
	Dead = 0,
	NotUpdated = 3
};

class FTrail
{
public:

	FTrail()
		: bForceEvaluateNextTick(true)
		, DrawInfo(nullptr)
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
	virtual TMap<FString, FInteractiveTrailTool*> GetTools() { return TMap<FString, FInteractiveTrailTool*>(); }
	virtual TRange<double> GetEffectiveRange() const { return TRange<double>::Empty(); }

	FTrajectoryDrawInfo* GetDrawInfo() { return DrawInfo.Get(); }
	void ForceEvaluateNextTick() { bForceEvaluateNextTick = true; }

protected:
	bool bForceEvaluateNextTick;
	TUniquePtr<FTrajectoryDrawInfo> DrawInfo;
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

class FConstantTrail : public FTrail
{
public:

	FConstantTrail()
		: FTrail()
		, CachedEffectiveRange(TRange<double>::Empty())
		, TrajectoryCache(MakeUnique<FArrayTrajectoryCache>(0.01, TRange<double>::Empty()))
	{}

	// FTrail interface
	virtual ETrailCacheState UpdateTrail(const FSceneContext& InSceneContext) override;
	virtual FTrajectoryCache* GetTrajectoryTransforms() override { return TrajectoryCache.Get(); }
	virtual TRange<double> GetEffectiveRange() const override { return CachedEffectiveRange; }

private:

	// FConstantTrail interface
	virtual ETrailCacheState UpdateState(const FSceneContext& InSceneContext) = 0;
	virtual const FTransform& GetConstantLocalTransform() const = 0;

	TRange<double> CachedEffectiveRange;
	TUniquePtr<FArrayTrajectoryCache> TrajectoryCache;
};

// TODO: take into account attach socket
class FConstantComponentTrail : public FConstantTrail 
{
public:
	
	FConstantComponentTrail(TWeakObjectPtr<class USceneComponent> InWeakComponent)
		: FConstantTrail()
		, WeakComponent(InWeakComponent)
		, LastLocalTransform(InWeakComponent->GetRelativeTransform())
	{}

private:
	
	// FConstantTrail interface
	virtual ETrailCacheState UpdateState(const FSceneContext& InSceneContext) override;
	virtual const FTransform& GetConstantLocalTransform() const override { return LastLocalTransform; }

	TWeakObjectPtr<USceneComponent> WeakComponent;
	FTransform LastLocalTransform;

};

} // namespace MovieScene
} // namespace UE
