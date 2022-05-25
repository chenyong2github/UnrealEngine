// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/ConstraintBaker.h"

#include "IControlRigObjectBinding.h"
#include "TransformConstraint.h"

#include "ISequencer.h"

#include "IKeyArea.h"
#include "LevelSequence.h"
#include "MovieSceneToolHelpers.h"
#include "TransformableHandle.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Constraints/ControlRigTransformableHandle.h"
#include "Tools/BakingHelper.h"
#include "Sections/MovieScene3DTransformSection.h"

#define LOCTEXT_NAMESPACE "ConstraintBaker"

namespace
{
	
UMovieScene3DTransformSection* GetTransformSection(const TSharedPtr<ISequencer>& InSequencer, AActor* InActor)
{
	if (!InSequencer || !InSequencer->GetFocusedMovieSceneSequence())
	{
		return nullptr;
	}
	
	const FGuid Guid = InSequencer->GetHandleToObject(InActor,true);
	if (!Guid.IsValid())
	{
		return nullptr;
	}
	
	return FBakingHelper::GetTransformSection(InSequencer.Get(), Guid);
}

void BakeComponent(
	const TSharedPtr<ISequencer>& InSequencer,
	const UTransformableComponentHandle* InComponentHandle,
	const TArray<FFrameNumber>& InFrames,
	const TArray<FTransform>& InTransforms,
	const EMovieSceneTransformChannel& InChannels)
{
	if (!InComponentHandle->IsValid())
	{
		return;
	}
	AActor* Actor = InComponentHandle->Component->GetOwner();
	if (!Actor)
	{
		return;
	}
	const UMovieScene3DTransformSection* TransformSection = GetTransformSection(InSequencer, Actor);
	if (!TransformSection)
	{
		return;
	}

	FBakingHelper::AddTransformKeys(TransformSection, InFrames, InTransforms, InChannels);
}

void BakeControl(
	const TSharedPtr<ISequencer>& InSequencer,
	const UTransformableControlHandle* InControlHandle,
	const TArray<FFrameNumber>& InFrames,
	const TArray<FTransform>& InLocalTransforms,
	const EMovieSceneTransformChannel& InChannels)
{
	if (!InControlHandle->IsValid())
	{
		return;
	}

	const UMovieScene* MovieScene = InSequencer->GetFocusedMovieSceneSequence()->GetMovieScene();

	FBakingHelper::AddTransformKeys(
		InControlHandle->ControlRig.Get(),
		InControlHandle->ControlName,
		InFrames,
		InLocalTransforms,
		InChannels,
		MovieScene->GetTickResolution());
}

EMovieSceneTransformChannel GetChannelsToKey(const UTickableTransformConstraint* InConstraint)
{
	static const TMap< ETransformConstraintType, EMovieSceneTransformChannel > ConstraintToChannels({
	{ETransformConstraintType::Translation, EMovieSceneTransformChannel::Translation},
	{ETransformConstraintType::Rotation, EMovieSceneTransformChannel::Rotation},
	{ETransformConstraintType::Scale, EMovieSceneTransformChannel::Scale},
	{ETransformConstraintType::Parent, EMovieSceneTransformChannel::AllTransform},
	{ETransformConstraintType::LookAt, EMovieSceneTransformChannel::Rotation}
	});

	const ETransformConstraintType ConstType = static_cast<ETransformConstraintType>(InConstraint->GetType());
	if (const EMovieSceneTransformChannel* Channel = ConstraintToChannels.Find(ConstType))
	{
		return *Channel;; 
	}
	
	return EMovieSceneTransformChannel::AllTransform;
}
	
}

void FConstraintBaker::DoIt(UTickableTransformConstraint* InConstraint)
{
	const TWeakPtr<ISequencer> WeakSequencer = FBakingHelper::GetSequencer();
	if (!WeakSequencer.IsValid() || !WeakSequencer.Pin()->GetFocusedMovieSceneSequence())
	{
		return;
	}
	const TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
	const UMovieScene* MovieScene = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene();
	if (!MovieScene)
	{
		return;
	}

	// compute frames
	const FFrameNumber StartFrame = MovieScene->GetPlaybackRange().GetLowerBoundValue();
	const FFrameNumber EndFrame = MovieScene->GetPlaybackRange().GetUpperBoundValue();
	TArray<FFrameNumber> Frames;
	FBakingHelper::CalculateFramesBetween(MovieScene, StartFrame, EndFrame, Frames);

	// compute transforms
	TArray<FTransform> Transforms;
	GetChildTransforms(Sequencer, *InConstraint, Frames, true, Transforms);

	// bake to channel curves
	const EMovieSceneTransformChannel Channels = GetChannelsToKey(InConstraint);
	BakeChild(Sequencer, InConstraint->ChildTRSHandle, Frames, Transforms, Channels);

	// disable constraint
	InConstraint->Active = false;
	InConstraint->ConstraintTick.SetTickFunctionEnable(false);
	
	// notify
	Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
}

void FConstraintBaker::GetChildTransforms(
	const TSharedPtr<ISequencer>& InSequencer,
	const UTickableTransformConstraint& InConstraint,
	const TArray<FFrameNumber>& InFrames,
	const bool bLocal,
	TArray<FTransform>& OutTransforms)
{
	const UMovieScene* MovieScene = InSequencer->GetFocusedMovieSceneSequence()->GetMovieScene();
	
	const FFrameRate TickResolution = MovieScene->GetTickResolution();
	const EMovieScenePlayerStatus::Type PlaybackStatus = InSequencer->GetPlaybackStatus();

	OutTransforms.SetNum(InFrames.Num());
	for (int32 Index = 0; Index < InFrames.Num(); ++Index)
	{
		const FFrameNumber& FrameNumber = InFrames[Index];
	
		// evaluate animation
		const FMovieSceneEvaluationRange EvaluationRange = FMovieSceneEvaluationRange(FFrameTime (FrameNumber), TickResolution);
		const FMovieSceneContext Context = FMovieSceneContext(EvaluationRange, PlaybackStatus).SetHasJumped(true);

		InSequencer->GetEvaluationTemplate().Evaluate(Context, *InSequencer);
	
		// evaluate constraints
		InConstraint.Evaluate();
	
		// evaluate ControlRig?
		// ControlRig->Evaluate_AnyThread();
		
		OutTransforms[Index] = bLocal ? InConstraint.GetChildLocalTransform() : InConstraint.GetChildGlobalTransform();
	}
}

void FConstraintBaker::BakeChild(
	const TSharedPtr<ISequencer>& InSequencer,
	UTransformableHandle* InHandle,
	const TArray<FFrameNumber>& InFrames,
	const TArray<FTransform>& InTransforms,
	const EMovieSceneTransformChannel& InChannels)
{
	if (const UTransformableComponentHandle* ComponentHandle = Cast<UTransformableComponentHandle>(InHandle))
	{
		return BakeComponent(InSequencer, ComponentHandle, InFrames, InTransforms, InChannels); 
	}
	
	if (const UTransformableControlHandle* ControlHandle = Cast<UTransformableControlHandle>(InHandle))
	{
		return BakeControl(InSequencer, ControlHandle, InFrames, InTransforms, InChannels); 
	}
}

#undef LOCTEXT_NAMESPACE


