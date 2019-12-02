// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayTrack.h"
#include "Insights/ViewModels/TimingEventsTrack.h"

#if WITH_ENGINE
#include "UObject/GCObject.h"
#include "Engine/World.h"
#endif

#if WITH_ENGINE
class UInsightsSkeletalMeshComponent;
#endif

class FAnimationSharedData;
class FTimingEventSearchParameters;
struct FSkeletalMeshPoseMessage;

class FSkeletalMeshPoseTrack : public TGameplayTrackMixin<FTimingEventsTrack>
#if WITH_ENGINE
	, public FGCObject
#endif
{
public:
	static const FName TypeName;
	static const FName SubTypeName;

	FSkeletalMeshPoseTrack(const FAnimationSharedData& InSharedData, uint64 InObjectID, const TCHAR* InName);
	~FSkeletalMeshPoseTrack();

	virtual void BuildDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context) override;
	virtual void Draw(const ITimingTrackDrawContext& Context) const override;
	virtual void InitTooltip(FTooltipDrawState& Tooltip, const ITimingEvent& HoveredTimingEvent) const override;
	virtual const TSharedPtr<const ITimingEvent> SearchEvent(const FTimingEventSearchParameters& InSearchParameters) const override;
	virtual void BuildContextMenu(FMenuBuilder& MenuBuilder) override;

	// Access drawing flags
	bool ShouldDrawPose() const { return bDrawPose; }
	bool ShouldDrawSkeleton() const { return bDrawSkeleton; }

#if WITH_ENGINE
	// Handle worlds being torn down
	void OnWorldCleanup(UWorld* InWorld, bool bSessionEnded, bool bCleanupResources);

	// Draw poses at the specified time
	void DrawPoses(UWorld* InWorld, double InTime);

	// FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("InsightsSkeletalMeshPoseTrack"); }
#endif

private:
	// Helper function used to find a skeletal mesh pose
	void FindSkeletalMeshPoseMessage(const FTimingEventSearchParameters& InParameters, TFunctionRef<void(double, double, uint32, const FSkeletalMeshPoseMessage&)> InFoundPredicate) const;

private:
	/** The shared data */
	const FAnimationSharedData& SharedData;

	/** The color to use to draw this track */
	FLinearColor Color;

	/** Whether to draw the pose */
	bool bDrawPose;

	/** Whether to draw the skeleton */
	bool bDrawSkeleton;

#if WITH_ENGINE
	/** Cached data per-world */
	struct FWorldComponentCache
	{
		FWorldComponentCache()
			: World(nullptr)
			, Component(nullptr)
			, Time(0.0)
		{}

		/** Get a cached component for this world */
		UInsightsSkeletalMeshComponent* GetComponent();

		/** The world we populate */
		UWorld* World;

		/** Cached component used to visualize in this world */
		UInsightsSkeletalMeshComponent* Component;

		/** The time we last cached on this component */
		double Time;
	};

	// Get the cached data for a world
	FWorldComponentCache& GetWorldCache(UWorld* InWorld);

	/** Cached map of per-world data */
	TMap<TWeakObjectPtr<UWorld>, FWorldComponentCache> WorldCache;

	/** Handle used to deal with world switching */
	FDelegateHandle OnWorldDestroyedHandle;
#endif
};