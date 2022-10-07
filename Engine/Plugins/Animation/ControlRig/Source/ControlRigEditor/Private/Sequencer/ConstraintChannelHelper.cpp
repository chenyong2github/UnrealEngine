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
#include "MovieSceneConstraintChannelHelper.h"
#include "MovieSceneSpawnableAnnotation.h"


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
		SmartComponentConstraintKey(InConstraint, WeakSequencer.Pin());
	}
	
	else if (const UTransformableControlHandle* ControlHandle = Cast<UTransformableControlHandle>(InConstraint->ChildTRSHandle))
	{
		SmartControlConstraintKey(InConstraint, WeakSequencer.Pin());
	}

	FConstraintChannelHelper::CreateBindingIDForHandle(InConstraint->ChildTRSHandle);
	FConstraintChannelHelper::CreateBindingIDForHandle(InConstraint->ParentTRSHandle);
}


void FConstraintChannelHelper::CreateBindingIDForHandle(UTransformableHandle* InHandle)
{
	const TWeakPtr<ISequencer> WeakSequencer = FBakingHelper::GetSequencer();
	if (InHandle == nullptr || WeakSequencer.IsValid() == false)
	{
		return;
	}
	if (USceneComponent* SceneComponent = Cast<USceneComponent>(InHandle->GetTarget().Get()))
	{
		AActor* Actor = SceneComponent->GetTypedOuter<AActor>();
		if (Actor)
		{
			TOptional<FMovieSceneSpawnableAnnotation> Spawnable = FMovieSceneSpawnableAnnotation::Find(Actor);
			if (Spawnable.IsSet())
			{
				// Check whether the spawnable is underneath the current sequence, if so, we can remap it to a local sequence ID
				InHandle->ConstraintBindingID = UE::MovieScene::FRelativeObjectBindingID(WeakSequencer.Pin()->GetFocusedTemplateID(), Spawnable->SequenceID, Spawnable->ObjectBindingID,
					*(WeakSequencer.Pin().Get()));
			}
			else
			{
				FGuid Guid = WeakSequencer.Pin()->GetHandleToObject(Actor, false); //don't create it???
				InHandle->ConstraintBindingID = UE::MovieScene::FRelativeObjectBindingID(Guid);
			}
		}
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

		// set constraint as dynamic
		InConstraint->bDynamicOffset = true;
		
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
				const bool bNeedsCompensation = InConstraint->NeedsCompensation();
				
				TGuardValue<bool> CompensateGuard(FMovieSceneConstraintChannelHelper::bDoNotCompensate, true);
				
				UControlRig* ControlRig = ControlHandle->ControlRig.Get();
				const FName& ControlName = ControlHandle->ControlName;
				
				// store the frames to compensate
				const TArrayView<FMovieSceneFloatChannel*> Channels = ControlHandle->GetFloatChannels(Section);
				TArray<FFrameNumber> FramesToCompensate;
				if (bNeedsCompensation)
				{
					FMovieSceneConstraintChannelHelper::GetFramesToCompensate<FMovieSceneFloatChannel>(Channel->ActiveChannel, ActiveValueToBeSet, Time, Channels, FramesToCompensate);
				}
				else
				{
					FramesToCompensate.Add(Time);
				}

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
					if (bNeedsCompensation && NumChannels > 0)
					{
						ChannelIndex = pChannelIndex->ChannelIndex;
						EvaluateTangentAtThisTime<FMovieSceneFloatChannel>(ChannelIndex, NumChannels, Section, Time, Tangents);
					}
				}
			
				const EMovieSceneTransformChannel ChannelsToKey =InConstraint->GetChannelsToKey();
				
				// add child's transform key at Time-1 to keep animation
				if (bNeedsCompensation)
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
					if (bNeedsCompensation && NumChannels > 0)
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

		// set constraint as dynamic
		InConstraint->bDynamicOffset = true;
		
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
				const bool bNeedsCompensation = InConstraint->NeedsCompensation();
				
				Section->Modify();

				//new for compensation

				TGuardValue<bool> CompensateGuard(FMovieSceneConstraintChannelHelper::bDoNotCompensate, true);

				// store the frames to compensate
				const TArrayView<FMovieSceneDoubleChannel*> Channels = ComponentHandle->GetDoubleChannels(Section);
				TArray<FFrameNumber> FramesToCompensate;
				if (bNeedsCompensation)
				{
					FMovieSceneConstraintChannelHelper::GetFramesToCompensate<FMovieSceneDoubleChannel>(Channel->ActiveChannel, ActiveValueToBeSet, Time, Channels, FramesToCompensate);
				}
				else
				{
					FramesToCompensate.Add(Time);
				}

				// store child and space transforms for these frames
				FCompensationEvaluator Evaluator(InConstraint);
				Evaluator.ComputeLocalTransforms(Actor->GetWorld(), InSequencer, FramesToCompensate, ActiveValueToBeSet);
				TArray<FTransform>& ChildLocals = Evaluator.ChildLocals;

				// store tangents at this time
				TArray<FMovieSceneTangentData> Tangents;
				const int32 ChannelIndex = 0;
				const int32 NumChannels = 9;

				if (bNeedsCompensation)
				{
					EvaluateTangentAtThisTime<FMovieSceneDoubleChannel>(ChannelIndex, NumChannels, Section, Time, Tangents);
				}

				const EMovieSceneTransformChannel ChannelsToKey = InConstraint->GetChannelsToKey();

				// add child's transform key at Time-1 to keep animation
				if (bNeedsCompensation)
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
					if (bNeedsCompensation)
					{
						SetTangentsAtThisTime<FMovieSceneDoubleChannel>(ChannelIndex,NumChannels, Section, Time, Tangents);
					}
				}
				// evaluate the constraint, this is needed so the global transform will be set up on the component 
				//Todo do we need to evaluate all constraints?
				InConstraint->SetActive(true); //will be false
				InConstraint->Evaluate();

				//need to fire this event so the transform values set by the constraint propragate to the transform section
				//first turn off autokey though
				EAutoChangeMode AutoChangeMode = InSequencer->GetAutoChangeMode();
				if (AutoChangeMode == EAutoChangeMode::AutoKey || AutoChangeMode == EAutoChangeMode::All)
				{
					InSequencer->SetAutoChangeMode(EAutoChangeMode::None);
				};
				FProperty* TransformProperty = FindFProperty<FProperty>(USceneComponent::StaticClass(), USceneComponent::GetRelativeLocationPropertyName());
				FEditPropertyChain PropertyChain;
				PropertyChain.AddHead(TransformProperty);
				FCoreUObjectDelegates::OnPreObjectPropertyChanged.Broadcast(Actor, PropertyChain);
				FPropertyChangedEvent PropertyChangedEvent(TransformProperty, EPropertyChangeType::ValueSet);
				FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(Actor, PropertyChangedEvent);
				InSequencer->RequestEvaluate();
				if (AutoChangeMode == EAutoChangeMode::AutoKey || AutoChangeMode == EAutoChangeMode::All)
				{
					InSequencer->SetAutoChangeMode(AutoChangeMode);
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
	const TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();

	IMovieSceneConstrainedSection* Section = nullptr;
	UWorld* World = nullptr;
	if (const UTransformableComponentHandle* ComponentHandle = Cast<UTransformableComponentHandle>(InConstraint->ChildTRSHandle))
	{

		AActor* Actor = ComponentHandle->Component->GetOwner();
		if (!Actor)
		{
			return;
		}
		World = Actor->GetWorld();
		const FTransform LocalTransform = ComponentHandle->GetLocalTransform();
		const FGuid Guid = Sequencer->GetHandleToObject(Actor, true);
		if (!Guid.IsValid())
		{
			return;
		}

		if (UMovieScene3DTransformSection* TransformSection = MovieSceneToolHelpers::GetTransformSection(Sequencer.Get(), Guid, LocalTransform))
		{
			Section = TransformSection;
		}
	}

	if (const UTransformableControlHandle* ControlHandle = Cast<UTransformableControlHandle>(InConstraint->ChildTRSHandle))
	{
		UControlRig* ControlRig = ControlHandle->ControlRig.LoadSynchronous();
		if (!ControlRig)
		{
			return;
		}
		World = ControlRig->GetWorld();
		if (UMovieSceneControlRigParameterSection* ControlSection = GetControlSection(ControlHandle,Sequencer))
		{
			Section = ControlSection;
		}
	}

	const FFrameRate TickResolution = Sequencer->GetFocusedTickResolution();
	const FFrameTime FrameTime = Sequencer->GetLocalTime().ConvertTo(TickResolution);
	const FFrameNumber Time = FrameTime.GetFrame();

	const TOptional<FFrameNumber> OptTime = bAllTimes ? TOptional<FFrameNumber>() : TOptional<FFrameNumber>(Time);
	CompensateIfNeeded(World, Sequencer, Section, OptTime);
}

void FConstraintChannelHelper::CompensateIfNeeded(
	UWorld* InWorld,
	const TSharedPtr<ISequencer>& InSequencer,
	IMovieSceneConstrainedSection* Section,
	const TOptional<FFrameNumber>& OptionalTime)
{
	if (FMovieSceneConstraintChannelHelper::bDoNotCompensate)
	{
		return;
	}

	TGuardValue<bool> CompensateGuard(FMovieSceneConstraintChannelHelper::bDoNotCompensate, true);

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

	bool bNeedsEvaluation = false;

	// gather all transform constraints
	TArray<FConstraintAndActiveChannel>& ConstraintChannels = Section->GetConstraintsChannels();
	TArray<FConstraintAndActiveChannel> TransformConstraintsChannels;
	Algo::CopyIf(ConstraintChannels, TransformConstraintsChannels,
		[](const FConstraintAndActiveChannel& InChannel)
		{
			if (!InChannel.Constraint.IsValid())
			{
				return false;
			}

			const UTickableTransformConstraint* Constraint = Cast<UTickableTransformConstraint>(InChannel.Constraint.Get());
			return Constraint && Constraint->NeedsCompensation();
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
				Evaluator.ComputeCompensation(InWorld, InSequencer, Time);
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
