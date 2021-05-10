// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieSceneTransformTrail.h"
#include "TrailHierarchy.h"
#include "MotionTrailEditorMode.h"

#include "ISequencer.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Systems/MovieSceneComponentTransformSystem.h"
#include "MovieSceneSequence.h"
#include "MovieSceneTracksComponentTypes.h"
#include "Sequencer/MovieSceneControlRigParameterSection.h"

#include "BaseGizmos/GizmoComponents.h"
#include "BaseGizmos/TransformGizmo.h"

#include "ViewportWorldInteraction.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"

namespace UE
{
namespace MotionTrailEditor
{

FMovieSceneTransformTrail::FMovieSceneTransformTrail(const FLinearColor& InColor, const bool bInIsVisible, TWeakObjectPtr<class UMovieSceneSection> InWeakSection, TSharedPtr<class ISequencer> InSequencer, const int32 InChannelOffset)
	: FTrail()
	, CachedEffectiveRange(TRange<double>::Empty())
	, DefaultTrailTool()
	, TrajectoryCache()
	, LastTransformSectionSig(InWeakSection->GetSignature())
	, CachedHierarchyGuid()
	, WeakSection(InWeakSection)
	, ChannelOffset(InChannelOffset)
	, WeakSequencer(InSequencer)
{
	DefaultTrailTool = MakeUnique<FDefaultMovieSceneTransformTrailTool>(this);
	TrajectoryCache = MakeUnique<FArrayTrajectoryCache>(0.01, GetEffectiveSectionRange());
	DrawInfo = MakeUnique<FTrajectoryDrawInfo>(InColor, TrajectoryCache.Get());
}

ETrailCacheState FMovieSceneTransformTrail::UpdateTrail(const FSceneContext& InSceneContext)
{
	CachedHierarchyGuid = InSceneContext.YourNode;
	UMovieSceneSection* Section = WeakSection.Get();
	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();

	FGuid SequencerBinding = FGuid();
	if (Sequencer)
	{ // TODO: expensive, but for some reason Section stays alive even after it is deleted
		Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene()->FindTrackBinding(*Cast<UMovieSceneTrack>(Section->GetOuter()), SequencerBinding);
	}

	checkf(InSceneContext.TrailHierarchy->GetHierarchy()[InSceneContext.YourNode].Parents.Num() == 1, TEXT("MovieSceneTransformTrails only support one parent"));
	const FGuid ParentGuid = InSceneContext.TrailHierarchy->GetHierarchy()[InSceneContext.YourNode].Parents[0];
	const TUniquePtr<FTrail>& Parent = InSceneContext.TrailHierarchy->GetAllTrails()[ParentGuid];

	ETrailCacheState ParentCacheState = InSceneContext.ParentCacheStates[ParentGuid];

	if (!Sequencer || !SequencerBinding.IsValid() || (ParentCacheState == ETrailCacheState::Dead))
	{
		return ETrailCacheState::Dead;
	}
	
	const bool bTrackUnchanged = Section->GetSignature() == LastTransformSectionSig;
	const bool bParentChanged = ParentCacheState != ETrailCacheState::UpToDate;

	ETrailCacheState CacheState;
	FTrailEvaluateTimes TempEvalTimes = InSceneContext.EvalTimes;
	if (!bTrackUnchanged || bParentChanged || bForceEvaluateNextTick)
	{
		if (DefaultTrailTool->IsActive())
		{
			DefaultTrailTool->OnSectionChanged();
		}

		const double Spacing = InSceneContext.EvalTimes.Spacing.Get(InSceneContext.TrailHierarchy->GetSecondsPerSegment());
		CachedEffectiveRange = TRange<double>::Hull({ Parent->GetEffectiveRange(), GetEffectiveSectionRange() });
		*TrajectoryCache = FArrayTrajectoryCache(Spacing, CachedEffectiveRange, FTransform::Identity * Parent->GetTrajectoryTransforms()->GetDefault()); // TODO:: Get channel default values
		TrajectoryCache->UpdateCacheTimes(TempEvalTimes);

		CacheState = ETrailCacheState::Stale;
		bForceEvaluateNextTick = false;
		LastTransformSectionSig = Section->GetSignature();
	}
	else 
	{
		TrajectoryCache->UpdateCacheTimes(TempEvalTimes);
	
		CacheState = ETrailCacheState::UpToDate;
	}

	if (TempEvalTimes.EvalTimes.Num() > 0)
	{
		UpdateCacheTimes(TempEvalTimes, Parent->GetTrajectoryTransforms());
	}

	if (DefaultTrailTool->IsActive())
	{
		DefaultTrailTool->UpdateKeysInRange(Parent->GetTrajectoryTransforms(), InSceneContext.TrailHierarchy->GetViewRange());
	}

	return CacheState;
}

TMap<FString, FInteractiveTrailTool*> FMovieSceneTransformTrail::GetTools()
{
	TMap<FString, FInteractiveTrailTool*> TempToolMap;
	TempToolMap.Add(UMotionTrailEditorMode::DefaultToolName, DefaultTrailTool.Get());
	return TempToolMap;
}

void FMovieSceneTransformTrail::AddReferencedObjects(FReferenceCollector & Collector)
{
	TArray<UObject*> ToolKeys = DefaultTrailTool->GetKeySceneComponents();
	Collector.AddReferencedObjects(ToolKeys);
}

UE::MovieScene::FIntermediate3DTransform FMovieSceneTransformTrail::CalculateDeltaToApply(const UE::MovieScene::FIntermediate3DTransform& Start, const UE::MovieScene::FIntermediate3DTransform& Current) const
{
	return UE::MovieScene::FIntermediate3DTransform(
		Current.GetTranslation() - Start.GetTranslation(),
		Current.GetRotation() - Start.GetRotation(),
		Current.GetScale() / Start.GetScale()
	);
}

TRange<double> FMovieSceneTransformTrail::GetEffectiveSectionRange() const
{
	UMovieSceneSection* TransformSection = WeakSection.Get();
	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
	check(TransformSection && Sequencer);

	TRange<FFrameNumber> EffectiveRange = TRange<FFrameNumber>::Empty();

	TArrayView<FMovieSceneFloatChannel*> Channels = TransformSection->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
	for (int32 ChannelIdx = ChannelOffset; ChannelIdx <= uint8(EMSTrailTransformChannel::MaxChannel) + ChannelOffset; ChannelIdx++)
	{
		FMovieSceneFloatChannel* Channel = Channels[ChannelIdx];
		EffectiveRange = TRange<FFrameNumber>::Hull(EffectiveRange, Channel->ComputeEffectiveRange());
	}

	EffectiveRange = TRange<FFrameNumber>::Intersection(EffectiveRange, TransformSection->GetRange());

	TRange<double> SectionRangeSeconds = TRange<double>(
		Sequencer->GetFocusedTickResolution().AsSeconds(EffectiveRange.GetLowerBoundValue()),
		Sequencer->GetFocusedTickResolution().AsSeconds(EffectiveRange.GetUpperBoundValue())
	);
		
	// TODO: clip by movie scene range? try movie keys outside of movie scene range

	return SectionRangeSeconds;
}

void FMovieSceneComponentTransformTrail::UpdateCacheTimes(const FTrailEvaluateTimes& EvaluateTimes, FTrajectoryCache* ParentTrajectoryCache)
{
	// TODO: re-populating the interrogator every frame is kind of inefficient
	Interrogator->ImportTrack(Cast<UMovieSceneTrack>(GetSection()->GetOuter()), UE::MovieScene::FInterrogationChannel::Default());

	for (const double Time : EvaluateTimes.EvalTimes)
	{
		const FFrameTime TickTime = Time * GetSequencer()->GetFocusedTickResolution();
		Interrogator->AddInterrogation(TickTime);
	}

	Interrogator->Update();

	TArray<UE::MovieScene::FIntermediate3DTransform> TempLocalTransforms;
	TempLocalTransforms.SetNum(EvaluateTimes.EvalTimes.Num());
	Interrogator->QueryLocalSpaceTransforms(UE::MovieScene::FInterrogationChannel::Default(), TempLocalTransforms);

	for (int32 Idx = 0; Idx < EvaluateTimes.EvalTimes.Num(); Idx++)
	{
		const FTransform TempLocalTransform = FTransform(TempLocalTransforms[Idx].GetRotation(), TempLocalTransforms[Idx].GetTranslation(), TempLocalTransforms[Idx].GetScale());
		FTransform TempWorldTransform = TempLocalTransform * ParentTrajectoryCache->Get(EvaluateTimes.EvalTimes[Idx] + KINDA_SMALL_NUMBER);
		TempWorldTransform.NormalizeRotation();
		GetTrajectoryTransforms()->Set(EvaluateTimes.EvalTimes[Idx] + KINDA_SMALL_NUMBER, TempWorldTransform); // The KINDA_SMALL_NUMBER is a hack to prevent rounding down when the calculated array index is just below a whole number
	}

	Interrogator->Reset();
}

UMovieScene3DTransformSection* FMovieSceneComponentTransformTrail::GetAbsoluteTransformSection(UMovieScene3DTransformTrack* TransformTrack)
{
	UMovieScene3DTransformSection* AbsoluteTransformSection = nullptr;
	for (UMovieSceneSection* Section : TransformTrack->GetAllSections())
	{
		UMovieScene3DTransformSection* TransformSection = Cast<UMovieScene3DTransformSection>(Section);
		check(Section);

		if (!TransformSection->GetBlendType().IsValid() || TransformSection->GetBlendType().Get() == EMovieSceneBlendType::Absolute)
		{
			AbsoluteTransformSection = TransformSection;
			break;
		}
	}

	check(AbsoluteTransformSection);

	return AbsoluteTransformSection;
}

FTransform FMovieSceneControlTransformTrail::EvaluateChannelsAtTime(TArrayView<FMovieSceneFloatChannel*> Channels, FFrameTime Time) const
{
	FVector TempTranslation;
	Channels[0]->Evaluate(Time, TempTranslation.X);
	Channels[1]->Evaluate(Time, TempTranslation.Y);
	Channels[2]->Evaluate(Time, TempTranslation.Z);
	FRotator TempRotation;
	Channels[3]->Evaluate(Time, TempRotation.Roll);
	Channels[4]->Evaluate(Time, TempRotation.Pitch);
	Channels[5]->Evaluate(Time, TempRotation.Yaw);
	FVector TempScale3D;
	Channels[6]->Evaluate(Time, TempScale3D.X);
	Channels[7]->Evaluate(Time, TempScale3D.Y);
	Channels[8]->Evaluate(Time, TempScale3D.Z);
	FTransform TempTransform = FTransform(TempRotation, TempTranslation, TempScale3D);
	TempTransform.NormalizeRotation();
	return TempTransform;
}

void FMovieSceneControlTransformTrail::UpdateCacheTimes(const FTrailEvaluateTimes& EvaluateTimes, FTrajectoryCache* ParentTrajectoryCache)
{
	// TODO: dirty skeleton root bone
	UMovieSceneControlRigParameterSection* Section = Cast<UMovieSceneControlRigParameterSection>(GetSection());
	check(Section);

	const FTransform InitialTransform = Section->GetControlRig()->GetControlHierarchy().GetInitialValue<FTransform>(ControlName);

	TArrayView<FMovieSceneFloatChannel*> FloatChannels = Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
	FloatChannels = FloatChannels.Slice(GetChannelOffset(), uint8(EMSTrailTransformChannel::MaxChannel) + 1);

	for (const double Time : EvaluateTimes.EvalTimes)
	{
		const FFrameTime TickTime = Time * GetSequencer()->GetFocusedTickResolution();
		const FTransform TempLocalTransform = EvaluateChannelsAtTime(FloatChannels, TickTime);
		FTransform TempWorldTransform = TempLocalTransform * InitialTransform * ParentTrajectoryCache->Get(Time + KINDA_SMALL_NUMBER);
		TempWorldTransform.NormalizeRotation();
		GetTrajectoryTransforms()->Set(Time + KINDA_SMALL_NUMBER, TempWorldTransform);
	}
}

UE::MovieScene::FIntermediate3DTransform FMovieSceneControlTransformTrail::CalculateDeltaToApply(const UE::MovieScene::FIntermediate3DTransform& Start, const UE::MovieScene::FIntermediate3DTransform& Current) const
{
	UMovieSceneControlRigParameterSection* Section = Cast<UMovieSceneControlRigParameterSection>(GetSection());

	const FTransform InitialTransform = Section->GetControlRig()->GetControlHierarchy().GetInitialValue<FTransform>(ControlName);
	const UE::MovieScene::FIntermediate3DTransform Delta = UE::MovieScene::FIntermediate3DTransform(
		Current.GetTranslation() - Start.GetTranslation(),
		Current.GetRotation() - Start.GetRotation(),
		Current.GetScale() / Start.GetScale()
	);

	const FTransform StartLocalTransform = InitialTransform.GetRelativeTransform(FTransform(Start.GetRotation(), Start.GetTranslation(), Start.GetScale()));
	const FTransform CurrentLocalTransform = InitialTransform.GetRelativeTransform(FTransform(Current.GetRotation(), Current.GetTranslation(), Current.GetScale()));
	const FTransform TempRelativeTransform = StartLocalTransform.GetRelativeTransform(CurrentLocalTransform);

	if (!Delta.GetRotation().IsNearlyZero() || !(Delta.GetScale() - FVector::OneVector).IsNearlyZero()) // Bit of a hack for now, assumes that only one of T/R/S will be changed at a time
	{
		return UE::MovieScene::FIntermediate3DTransform(FVector::ZeroVector, TempRelativeTransform.Rotator(), TempRelativeTransform.GetScale3D());
	}

	return UE::MovieScene::FIntermediate3DTransform(TempRelativeTransform.GetTranslation(), TempRelativeTransform.Rotator(), TempRelativeTransform.GetScale3D());
}

} // namespace MovieScene
} // namespace UE
