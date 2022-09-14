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
#include "MovieSceneConstraintChannelHelper.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Sequencer/MovieSceneControlRigParameterSection.h"
#include "ConstraintChannelHelper.h"
#include "ConstraintChannel.h"
#include "ConstraintChannelHelper.inl"

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


void FConstraintBaker::Bake(UWorld* InWorld, 
	UTickableTransformConstraint* InConstraint,
	const TSharedPtr<ISequencer>& InSequencer, 
	const TArray<FFrameNumber>& InFrames)
{
	if (InFrames.Num() == 0 || InConstraint == nullptr || InConstraint->ChildTRSHandle == nullptr)
	{
		return;
	}
	
	// compute transforms
	FCompensationEvaluator Evaluator(InConstraint);
	Evaluator.ComputeLocalTransformsForBaking(InWorld, InSequencer, InFrames);
	TArray<FTransform>& Transforms = Evaluator.ChildLocals;

	if (InFrames.Num() != Transforms.Num())
	{
		return;
	}
	//get the section to be used later to delete the extra transform keys at the frame -1 times, abort if not there for some reason
	UMovieSceneSection* Section = nullptr;
	if (const UTransformableControlHandle* ControlHandle = Cast<UTransformableControlHandle>(InConstraint->ChildTRSHandle))
	{
		Section = FConstraintChannelHelper::GetControlSection(ControlHandle, InSequencer);
	}
	else if (const UTransformableComponentHandle* ComponentHandle = Cast<UTransformableComponentHandle>(InConstraint->ChildTRSHandle))
	{ 
		//todo move to function also used by SmartConstraintKey
		AActor* Actor = ComponentHandle->Component->GetOwner();
		if (!Actor)
		{
			return;
		}

		const FTransform LocalTransform = ComponentHandle->GetLocalTransform();
		const FGuid Guid = InSequencer->GetHandleToObject(Actor, true);
		if (!Guid.IsValid())
		{
			return;
		}
		Section = MovieSceneToolHelpers::GetTransformSection(InSequencer.Get(), Guid, LocalTransform);
	}
	if (Section == nullptr)
	{
		return;
	}
	IMovieSceneConstrainedSection* ConstrainedSection = Cast<IMovieSceneConstrainedSection>(Section);
	if (ConstrainedSection == nullptr)
	{
		return;
	}
	FConstraintAndActiveChannel* ActiveChannel = ConstrainedSection->GetConstraintChannel(InConstraint->GetFName());
	if (ActiveChannel == nullptr)
	{
		return;
	}

	Section->Modify();

	// disable constraint and delete extra transform keys
	TMovieSceneChannelData<bool> ConstraintChannelData = ActiveChannel->ActiveChannel.GetData();
	TArrayView<const FFrameNumber> ConstraintFrames = ConstraintChannelData.GetTimes();

	// get transform channels
	const TArrayView<FMovieSceneFloatChannel*> FloatTransformChannels = InConstraint->ChildTRSHandle->GetFloatChannels(Section);
	const TArrayView<FMovieSceneDoubleChannel*> DoubleTransformChannels = InConstraint->ChildTRSHandle->GetDoubleChannels(Section);

	for (int32 Index = 0; Index < ConstraintFrames.Num(); ++Index)
	{
		const FFrameNumber Frame = ConstraintFrames[Index];
		//todo we need to add a key at the begin/end if there is no frame there
		if (Frame >= InFrames[0] && Frame <= InFrames[InFrames.Num() - 1])
		{
			ConstraintChannelData.UpdateOrAddKey(Frame, false);
			//delete minus one frames
			const FFrameNumber FrameMinusOne = Frame - 1;

			if (FloatTransformChannels.Num() > 0)
			{
				FMovieSceneConstraintChannelHelper::DeleteTransformKeys(FloatTransformChannels, FrameMinusOne);
			}
			else if (DoubleTransformChannels.Num() > 0)
			{
				FMovieSceneConstraintChannelHelper::DeleteTransformKeys(DoubleTransformChannels, FrameMinusOne);
			}
		}
	}
	// now bake to channel curves
	const EMovieSceneTransformChannel Channels = InConstraint->GetChannelsToKey();
	AddTransformKeys(InSequencer, InConstraint->ChildTRSHandle, InFrames, Transforms, Channels);

	// notify
	InSequencer->RequestEvaluate();
}

#undef LOCTEXT_NAMESPACE


