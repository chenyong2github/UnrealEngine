// Copyright Epic Games, Inc. All Rights Reserved.

#include "Trail.h"
#include "TrailHierarchy.h"
#include "MotionTrailEditorMode.h"

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


ETrailCacheState FConstantComponentTrail::UpdateTrail(const FTrail::FSceneContext& InSceneContext)
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
	const bool bParentChanged = CombinedParentStates != ETrailCacheState::Stale;
	const bool bLocalTransformChanged = !CurLocalTransform.Equals(LastLocalTransform);
	const bool bEvalRangeCached = TrajectoryCache->GetTrackRange().Contains(InSceneContext.EvalTimes.Range);

	FTrailEvaluateTimes TempEvalTimes = InSceneContext.EvalTimes;
	if (bLocalTransformChanged || bParentChanged || bForceEvaluateNextTick)
	{
		double Spacing = InSceneContext.EvalTimes.Spacing.Get(InSceneContext.TrailHierarchy->GetEditorMode()->GetTrailOptions()->SecondsPerSegment);
		CachedEffectiveRange = Parent->GetEffectiveRange();
		*TrajectoryCache = FArrayTrajectoryCache(Spacing, CachedEffectiveRange, CurLocalTransform * Parent->GetTrajectoryTransforms()->GetDefault());
		TrajectoryCache->UpdateCacheTimes(TempEvalTimes);
		LastLocalTransform = CurLocalTransform;

		bForceEvaluateNextTick = false;

		for (const double Time : TempEvalTimes.EvalTimes)
		{
			const FTransform TempWorldTransform = LastLocalTransform * Parent->GetTrajectoryTransforms()->Get(Time);
			TrajectoryCache->Set(Time, TempWorldTransform);
		}

		return ETrailCacheState::Stale;
	}
	else
	{
		return ETrailCacheState::UpToDate;
	}
}
