// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/ConstraintBaker.h"

#include "IControlRigObjectBinding.h"
#include "TransformConstraint.h"

#include "ISequencer.h"

#include "ControlRig.h"
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

	UMovieScene3DTransformSection* GetTransformSection(
		const TSharedPtr<ISequencer>& InSequencer,
		AActor* InActor,
		const FTransform& InTransform0)
	{
		if (!InSequencer || !InSequencer->GetFocusedMovieSceneSequence())
		{
			return nullptr;
		}

		const FGuid Guid = InSequencer->GetHandleToObject(InActor, true);
		if (!Guid.IsValid())
		{
			return nullptr;
		}

		return MovieSceneToolHelpers::GetTransformSection(InSequencer.Get(), Guid, InTransform0);
	}

	void BakeComponent(
		const TSharedPtr<ISequencer>& InSequencer,
		const UTransformableComponentHandle* InComponentHandle,
		const TArray<FFrameNumber>& InFrames,
		const TArray<FTransform>& InTransforms,
		const EMovieSceneTransformChannel& InChannels)
	{
		ensure(InTransforms.Num());

		if (!InComponentHandle->IsValid())
		{
			return;
		}
		AActor* Actor = InComponentHandle->Component->GetOwner();
		if (!Actor)
		{
			return;
		}
		UMovieScene3DTransformSection* TransformSection = GetTransformSection(InSequencer, Actor, InTransforms[0]);
		if (!TransformSection)
		{
			return;
		}
		TransformSection->Modify();
		const UMovieScene* MovieScene = InSequencer->GetFocusedMovieSceneSequence()->GetMovieScene();
		InComponentHandle->AddTransformKeys(InFrames, InTransforms, InChannels, MovieScene->GetTickResolution(), TransformSection, true);

	}

	void BakeControl(
		const TSharedPtr<ISequencer>& InSequencer,
		const UTransformableControlHandle* InControlHandle,
		const TArray<FFrameNumber>& InFrames,
		const TArray<FTransform>& InLocalTransforms,
		const EMovieSceneTransformChannel& InChannels)
	{
		ensure(InLocalTransforms.Num());

		if (!InControlHandle->IsValid())
		{
			return;
		}

		const UMovieScene* MovieScene = InSequencer->GetFocusedMovieSceneSequence()->GetMovieScene();
		UMovieSceneSection* Section = nullptr; //cnntrol rig doesn't need section it instead
		InControlHandle->AddTransformKeys(InFrames, InLocalTransforms, InChannels, MovieScene->GetTickResolution(), Section, true);

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
	MovieSceneToolHelpers::CalculateFramesBetween(MovieScene, StartFrame, EndFrame, Frames);

	if (Frames.IsEmpty())
	{
		return;
	}

	// compute transforms
	TArray<FTransform> Transforms;
	GetHandleTransforms(Sequencer, InConstraint->ChildTRSHandle, {InConstraint}, Frames, true, Transforms);
	
	if (Frames.Num() != Transforms.Num())
	{
		return;
	}

	// bake to channel curves
	const EMovieSceneTransformChannel Channels = InConstraint->GetChannelsToKey();
	AddTransformKeys(Sequencer, InConstraint->ChildTRSHandle, Frames, Transforms, Channels);

	// disable constraint
	InConstraint->SetActive(false);
	
	// notify
	Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
}

void FConstraintBaker::GetHandleTransforms(
	UWorld* InWorld,
	const TSharedPtr<ISequencer>& InSequencer,
	const UTransformableHandle* InHandle,
	const TArray<FFrameNumber>& InFrames,
	const bool bLocal,
	TArray<FTransform>& OutTransforms)
{
	const FConstraintsManagerController& Controller = FConstraintsManagerController::Get(InWorld);
	static constexpr bool bSorted = true;
	TArray<TObjectPtr<UTickableConstraint>> Constraints = Controller.GetParentConstraints(InHandle->GetHash(), bSorted);

	TArray<UTickableTransformConstraint*> TransformConstraints;
	for (const TObjectPtr<UTickableConstraint>& Constraint: Constraints)
	{
		if (UTickableTransformConstraint* TransformConstraint = Cast<UTickableTransformConstraint>(Constraint.Get()))
		{
			TransformConstraints.Add(TransformConstraint);
		}
	}
	
	return GetHandleTransforms(InSequencer, InHandle, TransformConstraints, InFrames, bLocal, OutTransforms);
}

void FConstraintBaker::GetHandleTransforms(
	const TSharedPtr<ISequencer>& InSequencer,
	const UTransformableHandle* InHandle,
	const TArray<UTickableTransformConstraint*>& InConstraintsToEvaluate,
	const TArray<FFrameNumber>& InFrames,
	const bool bLocal,
	TArray<FTransform>& OutTransforms)
{
	// if (InConstraintsToEvaluate.IsEmpty())
	// {
	// 	return;
	// }
	
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
		for (const UTickableTransformConstraint* Constraint: InConstraintsToEvaluate)
		{
			Constraint->Evaluate();
		}
	
		// evaluate ControlRig?
		// ControlRig->Evaluate_AnyThread();
		
		OutTransforms[Index] = bLocal ? InHandle->GetLocalTransform() : InHandle->GetGlobalTransform();
	}
}

void FConstraintBaker::AddTransformKeys(
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


