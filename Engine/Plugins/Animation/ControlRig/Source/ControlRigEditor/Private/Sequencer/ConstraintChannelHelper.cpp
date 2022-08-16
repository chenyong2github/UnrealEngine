// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConstraintChannelHelper.inl"
#include "ConstraintChannelHelper.h"
#include "ControlRigSpaceChannelEditors.h"
#include "ISequencer.h"

#include "Constraints/ControlRigTransformableHandle.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "LevelSequence.h"
#include "MovieSceneToolHelpers.h"

#include "TransformConstraint.h"
#include "Algo/Copy.h"

#include "Tools/BakingHelper.h"
#include "Tools/ConstraintBaker.h"
#include "ScopedTransaction.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneDoubleChannel.h"

bool FConstraintChannelHelper::bDoNotCompensate = false;

#define LOCTEXT_NAMESPACE "Constraints"

namespace
{
	bool CanAddKey(const FMovieSceneBoolChannel& ActiveChannel, const FFrameNumber& InTime, bool& ActiveValue)
	{
		const TMovieSceneChannelData<const bool> ChannelData = ActiveChannel.GetData();
		const TArrayView<const FFrameNumber> Times = ChannelData.GetTimes();
		if (Times.IsEmpty())
		{
			ActiveValue = true;
			return true;
		}

		const TArrayView<const bool> Values = ChannelData.GetValues();
		if (InTime < Times[0])
		{
			if (!Values[0])
			{
				ActiveValue = true;
				return true;
			}
			return false;
		}
		
		if (InTime > Times.Last())
		{
			ActiveValue = !Values.Last();
			return true;
		}

		return false;
	}
}

bool FConstraintChannelHelper::IsKeyframingAvailable()
{
	const TWeakPtr<ISequencer> WeakSequencer = FBakingHelper::GetSequencer();
	if (!WeakSequencer.IsValid())
	{
		return false;
	}

	if (!WeakSequencer.Pin()->GetFocusedMovieSceneSequence())
	{
		return false;
	}

	return true;
}

void FConstraintChannelHelper::SmartConstraintKey(UTickableTransformConstraint* InConstraint)
{
	const TWeakPtr<ISequencer> WeakSequencer = FBakingHelper::GetSequencer();
	if (!WeakSequencer.IsValid() || !WeakSequencer.Pin()->GetFocusedMovieSceneSequence())
	{
		return;
	}

	if (const UTransformableComponentHandle* ComponentHandle = Cast<UTransformableComponentHandle>(InConstraint->ChildTRSHandle))
	{
		return SmartComponentConstraintKey(InConstraint, WeakSequencer.Pin());
	}
	
	if (const UTransformableControlHandle* ControlHandle = Cast<UTransformableControlHandle>(InConstraint->ChildTRSHandle))
	{
		return SmartControlConstraintKey(InConstraint, WeakSequencer.Pin());
	}
}


UMovieSceneControlRigParameterSection* FConstraintChannelHelper::GetControlSection(
	const UTransformableControlHandle* InHandle,
	const TSharedPtr<ISequencer>& InSequencer)
{
	const UMovieScene* MovieScene = InSequencer->GetFocusedMovieSceneSequence()->GetMovieScene();
	if (!MovieScene)
	{
		return nullptr;
	}

	const TWeakObjectPtr<UControlRig> ControlRig = InHandle->ControlRig.LoadSynchronous();
	if(ControlRig.IsValid())
	{	
		const TArray<FMovieSceneBinding>& Bindings = MovieScene->GetBindings();
		for (const FMovieSceneBinding& Binding : Bindings)
		{
			UMovieSceneTrack* Track = MovieScene->FindTrack(UMovieSceneControlRigParameterTrack::StaticClass(), Binding.GetObjectGuid());
			UMovieSceneControlRigParameterTrack* ControlRigTrack = Cast<UMovieSceneControlRigParameterTrack>(Track);
			if (ControlRigTrack && ControlRigTrack->GetControlRig() == ControlRig)
			{
				return Cast<UMovieSceneControlRigParameterSection>(ControlRigTrack->FindSection(0));
			}
		}
	}

	return nullptr;
}

UMovieScene3DTransformSection* FConstraintChannelHelper::GetTransformSection(
	const UTransformableComponentHandle* InHandle,
	const TSharedPtr<ISequencer>& InSequencer)
{
	AActor* Actor = InHandle->Component->GetOwner();
	if (!Actor)
	{
		return nullptr;
	}
	
	const FGuid Guid = InSequencer->GetHandleToObject(Actor,true);
	if (!Guid.IsValid())
	{
		return nullptr;
	}
	
	return MovieSceneToolHelpers::GetTransformSection(InSequencer.Get(), Guid);
}

void FConstraintChannelHelper::SmartControlConstraintKey(
	UTickableTransformConstraint* InConstraint,
	const TSharedPtr<ISequencer>& InSequencer)
{
	const UTransformableControlHandle* ControlHandle = Cast<UTransformableControlHandle>(InConstraint->ChildTRSHandle);
	if (!ControlHandle)
	{
		return;
	}
	
	if (UMovieSceneControlRigParameterSection* Section = GetControlSection(ControlHandle, InSequencer))
	{
		FScopedTransaction Transaction(LOCTEXT("KeyConstraintaKehy", "Key Constraint Key"));
		Section->Modify();
		// add the channel
		Section->AddConstraintChannel(InConstraint);

		// add key if needed
		if (FConstraintAndActiveChannel* Channel = Section->GetConstraintChannel(InConstraint->GetFName()))
		{
			const FFrameRate TickResolution = InSequencer->GetFocusedTickResolution();
			const FFrameTime FrameTime = InSequencer->GetLocalTime().ConvertTo(TickResolution);
			const FFrameNumber Time = FrameTime.GetFrame();

			bool ActiveValueToBeSet = false;
			if (CanAddKey(Channel->ActiveChannel, Time, ActiveValueToBeSet))
			{

				TGuardValue<bool> CompensateGuard(bDoNotCompensate, true);
				
				// set constraint as dynamic
				InConstraint->bDynamicOffset = true;
				
				UControlRig* ControlRig = ControlHandle->ControlRig.Get();
				const FName& ControlName = ControlHandle->ControlName;
				
				// store the frames to compensate
				const TArrayView<FMovieSceneFloatChannel*> Channels = ControlHandle->GetFloatChannels(Section);
				TArray<FFrameNumber> FramesToCompensate;
				FMovieSceneConstraintChannelHelper::GetFramesToCompensate<FMovieSceneFloatChannel>(Channel->ActiveChannel, ActiveValueToBeSet, Time, Channels, FramesToCompensate);

				// store child and space transforms for these frames
				FCompensationEvaluator Evaluator(InConstraint);
				Evaluator.ComputeLocalTransforms(ControlRig->GetWorld(), InSequencer, FramesToCompensate, ActiveValueToBeSet);
				TArray<FTransform>& ChildLocals = Evaluator.ChildLocals;
				
				// store tangents at this time
				TArray<FMovieSceneTangentData> Tangents;
				int32 ChannelIndex = 0, NumChannels = 0;

				FChannelMapInfo* pChannelIndex = nullptr;
				FRigControlElement* ControlElement = nullptr;
				Tie(ControlElement, pChannelIndex) = FControlRigSpaceChannelHelpers::GetControlAndChannelInfo(ControlRig, Section, ControlName);

				if (pChannelIndex && ControlElement)
				{
					// get the number of float channels to treat
					NumChannels = FControlRigSpaceChannelHelpers::GetNumFloatChannels(ControlElement->Settings.ControlType);
					if (NumChannels > 0)
					{
						ChannelIndex = pChannelIndex->ChannelIndex;
						EvaluateTangentAtThisTime<FMovieSceneFloatChannel>(ChannelIndex, NumChannels, Section, Time, Tangents);
					}
				}
			
				const EMovieSceneTransformChannel ChannelsToKey =InConstraint->GetChannelsToKey();
				
				// add child's transform key at Time-1 to keep animation
				{
					const FFrameNumber TimeMinusOne(Time - 1);

					ControlHandle->AddTransformKeys({ TimeMinusOne },
						{ ChildLocals[0] }, ChannelsToKey, TickResolution, nullptr,true);

					// set tangents at Time-1
					if (NumChannels > 0)
					{
						SetTangentsAtThisTime<FMovieSceneFloatChannel>(ChannelIndex, NumChannels, Section, TimeMinusOne, Tangents);
					}
				}

				// add active key
				{
					TMovieSceneChannelData<bool> ChannelData = Channel->ActiveChannel.GetData();
					ChannelData.AddKey(Time, ActiveValueToBeSet);
				}

				// compensate
				{
					// we need to remove the first transforms as we store NumFrames+1 transforms
					ChildLocals.RemoveAt(0);

					// add keys
					ControlHandle->AddTransformKeys(FramesToCompensate,
						ChildLocals, ChannelsToKey, TickResolution, nullptr,true);

					// set tangents at Time
					if (NumChannels > 0 )
					{
						SetTangentsAtThisTime<FMovieSceneFloatChannel>(ChannelIndex, NumChannels, Section, Time, Tangents);
					}
				}
			}
		}
	}
}

void FConstraintChannelHelper::SmartComponentConstraintKey(
	UTickableTransformConstraint* InConstraint,
	const TSharedPtr<ISequencer>& InSequencer)
{
	const UTransformableComponentHandle* ComponentHandle = Cast<UTransformableComponentHandle>(InConstraint->ChildTRSHandle);
	if (!ComponentHandle)
	{
		return;
	}
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

	if (UMovieScene3DTransformSection* Section = MovieSceneToolHelpers::GetTransformSection(InSequencer.Get(), Guid, LocalTransform))
	{
		FScopedTransaction Transaction(LOCTEXT("KeyConstraintaKehy", "Key Constraint Key"));
		Section->Modify();
		// add the channel
		Section->AddConstraintChannel(InConstraint);

		// add key if needed
		if (FConstraintAndActiveChannel* Channel = Section->GetConstraintChannel(InConstraint->GetFName()))
		{
			const FFrameRate TickResolution = InSequencer->GetFocusedTickResolution();
			const FFrameTime FrameTime = InSequencer->GetLocalTime().ConvertTo(TickResolution);
			const FFrameNumber Time = FrameTime.GetFrame();

			bool ActiveValueToBeSet = false;
			if (CanAddKey(Channel->ActiveChannel, Time, ActiveValueToBeSet))
			{
	
				Section->Modify();

				//new for compensatio

				TGuardValue<bool> CompensateGuard(bDoNotCompensate, true);

				// set constraint as dynamic
				InConstraint->bDynamicOffset = true;

				// store the frames to compensate
				const TArrayView<FMovieSceneDoubleChannel*> Channels = ComponentHandle->GetDoubleChannels(Section);
				TArray<FFrameNumber> FramesToCompensate;
				FMovieSceneConstraintChannelHelper::GetFramesToCompensate<FMovieSceneDoubleChannel>(Channel->ActiveChannel, ActiveValueToBeSet, Time, Channels, FramesToCompensate);

				// store child and space transforms for these frames
				FCompensationEvaluator Evaluator(InConstraint);
				Evaluator.ComputeLocalTransforms(Actor->GetWorld(), InSequencer, FramesToCompensate, ActiveValueToBeSet);
				TArray<FTransform>& ChildLocals = Evaluator.ChildLocals;

				// store tangents at this time
				TArray<FMovieSceneTangentData> Tangents;
				const int32 ChannelIndex = 0;
				const int32 NumChannels = 9;
				EvaluateTangentAtThisTime<FMovieSceneDoubleChannel>(ChannelIndex, NumChannels, Section, Time, Tangents);

				const EMovieSceneTransformChannel ChannelsToKey = InConstraint->GetChannelsToKey();

				// add child's transform key at Time-1 to keep animation
				{
					const FFrameNumber TimeMinusOne(Time - 1);

					MovieSceneToolHelpers::AddTransformKeys(Section, { TimeMinusOne }, { ChildLocals[0] }, ChannelsToKey);

					SetTangentsAtThisTime<FMovieSceneDoubleChannel>(ChannelIndex, NumChannels, Section, TimeMinusOne, Tangents);
				}

				// add active key
				{
					TMovieSceneChannelData<bool> ChannelData = Channel->ActiveChannel.GetData();
					ChannelData.AddKey(Time, ActiveValueToBeSet);
				}

				// compensate
				{
					// we need to remove the first transforms as we store NumFrames+1 transforms
					ChildLocals.RemoveAt(0);

					// add keys
					MovieSceneToolHelpers::AddTransformKeys(Section, FramesToCompensate, ChildLocals, ChannelsToKey);

					// set tangents at Time
					SetTangentsAtThisTime<FMovieSceneDoubleChannel>(ChannelIndex,NumChannels, Section, Time, Tangents);
				}
			}
		}
	}
}

void FConstraintChannelHelper::Compensate(UTickableTransformConstraint* InConstraint, const bool bAllTimes)
{
	const TWeakPtr<ISequencer> WeakSequencer = FBakingHelper::GetSequencer();
	if (!WeakSequencer.IsValid() || !WeakSequencer.Pin()->GetFocusedMovieSceneSequence())
	{
		return;
	}

	const UTransformableControlHandle* ControlHandle = Cast<UTransformableControlHandle>(InConstraint->ChildTRSHandle);
	if (!ControlHandle)
	{
		return;
	}

	const TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
	if (const UMovieSceneControlRigParameterSection* Section = GetControlSection(ControlHandle, Sequencer))
	{
		const FFrameRate TickResolution = Sequencer->GetFocusedTickResolution();
		const FFrameTime FrameTime = Sequencer->GetLocalTime().ConvertTo(TickResolution);
		const FFrameNumber Time = FrameTime.GetFrame();

		const TOptional<FFrameNumber> OptTime = bAllTimes ? TOptional<FFrameNumber>() : TOptional<FFrameNumber>(Time);
		CompensateIfNeeded(ControlHandle->ControlRig.Get(), Sequencer, Section, OptTime);
	}
}

void FConstraintChannelHelper::CompensateIfNeeded(
	const UControlRig* ControlRig,
	const TSharedPtr<ISequencer>& InSequencer,
	const UMovieSceneControlRigParameterSection* Section,
	const TOptional<FFrameNumber>& OptionalTime)
{
	if (bDoNotCompensate)
	{
		return;
	}

	TGuardValue<bool> CompensateGuard(bDoNotCompensate, true);

	// Frames to compensate
	TArray<FFrameNumber> OptionalTimeArray;
	if (OptionalTime.IsSet())
	{
		OptionalTimeArray.Add(OptionalTime.GetValue());
	}

	auto GetSpaceTimesToCompensate = [&OptionalTimeArray](const FConstraintAndActiveChannel& Channel)->TArrayView<const FFrameNumber>
	{
		if (OptionalTimeArray.IsEmpty())
		{
			return Channel.ActiveChannel.GetData().GetTimes();
		}
		return OptionalTimeArray;
	};

	// keyframe context
	FRigControlModifiedContext KeyframeContext;
	KeyframeContext.SetKey = EControlRigSetKey::Always;
	const FFrameRate TickResolution = InSequencer->GetFocusedTickResolution();

	bool bNeedsEvaluation = false;

	// gather all transform constraints
	TArray<FConstraintAndActiveChannel> TransformConstraintsChannels;
	Algo::CopyIf(Section->GetConstraintsChannels(), TransformConstraintsChannels,
		[](const FConstraintAndActiveChannel& InChannel)
		{
			return InChannel.Constraint.IsValid() && InChannel.Constraint->IsA<UTickableTransformConstraint>();
		}
	);

	// compensate constraints
	for (const FConstraintAndActiveChannel& Channel : TransformConstraintsChannels)
	{
		const TArrayView<const FFrameNumber> FramesToCompensate = GetSpaceTimesToCompensate(Channel);
		for (const FFrameNumber& Time : FramesToCompensate)
		{
			const FFrameNumber TimeMinusOne(Time - 1);

			bool CurrentValue = false, PreviousValue = false;
			Channel.ActiveChannel.Evaluate(TimeMinusOne, PreviousValue);
			Channel.ActiveChannel.Evaluate(Time, CurrentValue);

			if (CurrentValue != PreviousValue) //if they are the same no need to do anything
			{
				UTickableTransformConstraint* Constraint = Cast<UTickableTransformConstraint>(Channel.Constraint.Get());

				// compute transform to set
				// if switching from active to inactive then we must add a key at T-1 in the constraint space
				// if switching from inactive to active then we must add a key at T-1 in the previous constraint or parent space
				FCompensationEvaluator Evaluator(Constraint);
				Evaluator.ComputeCompensation(ControlRig->GetWorld(), InSequencer, Time);
				const TArray<FTransform>& LocalTransforms = Evaluator.ChildLocals;

				const EMovieSceneTransformChannel ChannelsToKey = Constraint->GetChannelsToKey();
				FConstraintBaker::AddTransformKeys(
					InSequencer, Constraint->ChildTRSHandle, { TimeMinusOne }, LocalTransforms, ChannelsToKey);
				bNeedsEvaluation = true;
			}
		}
	}

	if (bNeedsEvaluation)
	{
		InSequencer->ForceEvaluate();
	}
}

#undef LOCTEXT_NAMESPACE
