// Copyright Epic Games, Inc. All Rights Reserved.

#include "Trail.h"
#include "TrailHierarchy.h"
#include "MotionTrailEditorMode.h"

namespace UE
{
namespace MotionTrailEditor
{

ETrailCacheState FRootTrail::UpdateTrail(const FSceneContext& InSceneContext)
{
	if (bForceEvaluateNextTick)
	{
		bForceEvaluateNextTick = false;
		return ETrailCacheState::Stale;
	}
	else
	{
		return ETrailCacheState::UpToDate;
	}
}

ETrailCacheState FConstantTrail::UpdateTrail(const FTrail::FSceneContext& InSceneContext)
{
	const TUniquePtr<FTrail>& Parent = InSceneContext.TrailHierarchy->GetAllTrails()[InSceneContext.TrailHierarchy->GetHierarchy()[InSceneContext.YourNode].Parents[0]];

	const ETrailCacheState CacheState = UpdateState(InSceneContext);
	if (CacheState == ETrailCacheState::Dead)
	{
		return CacheState;
	}

	const bool bEvalRangeCached = TrajectoryCache->GetTrackRange().Contains(InSceneContext.EvalTimes.Range);

	FTrailEvaluateTimes TempEvalTimes = InSceneContext.EvalTimes;
	if (CacheState == ETrailCacheState::Stale)
	{
		double Spacing = InSceneContext.EvalTimes.Spacing.Get(InSceneContext.TrailHierarchy->GetSecondsPerSegment());
		CachedEffectiveRange = Parent->GetEffectiveRange();
		*TrajectoryCache = FArrayTrajectoryCache(Spacing, CachedEffectiveRange, GetConstantLocalTransform() * Parent->GetTrajectoryTransforms()->GetDefault());
		TrajectoryCache->UpdateCacheTimes(TempEvalTimes);

		for (const double Time : TempEvalTimes.EvalTimes)
		{
			const FTransform TempWorldTransform = GetConstantLocalTransform() * Parent->GetTrajectoryTransforms()->Get(Time);
			TrajectoryCache->Set(Time, TempWorldTransform);
		}
	}
	
	return CacheState;
}

ETrailCacheState FConstantComponentTrail::UpdateState(const FSceneContext& InSceneContext)
{
	const TUniquePtr<FTrail>& Parent = InSceneContext.TrailHierarchy->GetAllTrails()[InSceneContext.TrailHierarchy->GetHierarchy()[InSceneContext.YourNode].Parents[0]];

	ETrailCacheState CombinedParentStates = ETrailCacheState::UpToDate;
	for (const TPair<FGuid, ETrailCacheState>& GuidStatePair : InSceneContext.ParentCacheStates)
	{
		if (GuidStatePair.Value == ETrailCacheState::Dead)
		{
			CombinedParentStates = ETrailCacheState::Dead;
			break;
		}
		else if (GuidStatePair.Value == ETrailCacheState::Stale)
		{
			CombinedParentStates = ETrailCacheState::Stale;
		}
	}

	if (!WeakComponent.IsValid() || CombinedParentStates == ETrailCacheState::Dead)
	{
		return ETrailCacheState::Dead;
	}

	const FTransform CurLocalTransform = WeakComponent->GetRelativeTransform();
	const bool bParentChanged = CombinedParentStates != ETrailCacheState::UpToDate;
	const bool bLocalTransformChanged = !CurLocalTransform.Equals(LastLocalTransform);
	
	if (bLocalTransformChanged || bParentChanged || bForceEvaluateNextTick)
	{
		LastLocalTransform = CurLocalTransform;
		bForceEvaluateNextTick = false;

		return ETrailCacheState::Stale;
	}
	else
	{
		return ETrailCacheState::UpToDate;
	}
}

} // namespace MovieScene
} // namespace UE
