// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trail.h"
#include "MotionTrailEditorToolset.h"
#include "TrajectoryDrawInfo.h"
#include "MovieSceneTransformTrailTool.h"

#include "BaseGizmos/TransformProxy.h"
#include "MovieSceneTracksComponentTypes.h"
#include "EntitySystem/Interrogation/MovieSceneInterrogationLinker.h"
#include "UObject/GCObject.h"

class ISequencer;
class UMovieSceneSection;
class UMovieScene3DTransformTrack;
class UMovieScene3DTransformSection;

namespace UE
{
namespace MotionTrailEditor
{

class FMovieSceneTransformTrail : public FTrail, public FGCObject
{
public:
	FMovieSceneTransformTrail(const FLinearColor& InColor, const bool bInIsVisible, TWeakObjectPtr<UMovieSceneSection> InWeakSection, TSharedPtr<ISequencer> InSequencer, const int32 InChannelOffset = 0);

	// Begin FTrail interface
	virtual ETrailCacheState UpdateTrail(const FSceneContext& InSceneContext) override;
	virtual FTrajectoryCache* GetTrajectoryTransforms() override { return TrajectoryCache.Get(); }
	virtual TMap<FString, FInteractiveTrailTool*> GetTools() override;
	virtual TRange<double> GetEffectiveRange() const override { return CachedEffectiveRange; }
	// End FTrail interface

	// Begin FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	// End FGCObject interface

	TSharedPtr<ISequencer> GetSequencer() const { return WeakSequencer.Pin(); }
	FGuid GetCachedHierarchyGuid() const { return CachedHierarchyGuid; }
	UMovieSceneSection* GetSection() const { return WeakSection.Get(); }
	int32 GetChannelOffset() const { return ChannelOffset; }

	friend class FDefaultMovieSceneTransformTrailTool;

private:
	// Begin FMovieSceneTransformTrail interface
	virtual void UpdateCacheTimes(const FTrailEvaluateTimes& EvaluateTimes, FTrajectoryCache* ParentTrajectoryCache) = 0;
	virtual UE::MovieScene::FIntermediate3DTransform CalculateDeltaToApply(const UE::MovieScene::FIntermediate3DTransform& Start, const UE::MovieScene::FIntermediate3DTransform& Current) const;
	// End FMovieSceneTransformTrail interface

	TRange<double> GetEffectiveSectionRange() const;
	TRange<double> CachedEffectiveRange;

	TUniquePtr<FDefaultMovieSceneTransformTrailTool> DefaultTrailTool;
	TUniquePtr<FArrayTrajectoryCache> TrajectoryCache;

	FGuid LastTransformSectionSig;
	FGuid CachedHierarchyGuid;
	TWeakObjectPtr<UMovieSceneSection> WeakSection;
	int32 ChannelOffset;
	TWeakPtr<ISequencer> WeakSequencer;
};

class FMovieSceneComponentTransformTrail : public FMovieSceneTransformTrail
{
public:

	FMovieSceneComponentTransformTrail(const FLinearColor& InColor, const bool bInIsVisible, TWeakObjectPtr<UMovieSceneSection> InWeakSection, TSharedPtr<ISequencer> InSequencer, const int32 InChannelOffset = 0)
		: FMovieSceneTransformTrail(InColor, bInIsVisible, InWeakSection, InSequencer, InChannelOffset)
		, Interrogator(MakeUnique<UE::MovieScene::FSystemInterrogator>())
	{}

	static UMovieScene3DTransformSection* GetAbsoluteTransformSection(UMovieScene3DTransformTrack* TransformTrack);

private:

	// Begin FMovieSceneTransformTrail interface
	virtual void UpdateCacheTimes(const FTrailEvaluateTimes& EvaluateTimes, FTrajectoryCache* ParentTrajectoryCache) override;
	// End FMovieSceneTransformTrail interface

	TUniquePtr<UE::MovieScene::FSystemInterrogator> Interrogator;
};

class FMovieSceneControlTransformTrail : public FMovieSceneTransformTrail
{
public:

	FMovieSceneControlTransformTrail(const FLinearColor& InColor, const bool bInIsVisible, TWeakObjectPtr<UMovieSceneSection> InWeakSection, TSharedPtr<ISequencer> InSequencer, const int32 InChannelOffset, const FName& InControlName)
		: FMovieSceneTransformTrail(InColor, bInIsVisible, InWeakSection, InSequencer, InChannelOffset)
		, ControlName(InControlName)
	{}

private:
	FTransform EvaluateChannelsAtTime(TArrayView<FMovieSceneFloatChannel*> Channels, FFrameTime Time) const;

	// Begin FMovieSceneTransformTrail interface
	virtual void UpdateCacheTimes(const FTrailEvaluateTimes& EvaluateTimes, FTrajectoryCache* ParentTrajectoryCache) override;
	virtual UE::MovieScene::FIntermediate3DTransform CalculateDeltaToApply(const UE::MovieScene::FIntermediate3DTransform& Start, const UE::MovieScene::FIntermediate3DTransform& Current) const override;
	// End FMovieSceneTransformTrail interface

	FName ControlName;
};

} // namespace MovieScene
} // namespace UE
