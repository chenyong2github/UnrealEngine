// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConstraintChannelHelper.h"

#include "ActorForWorldTransforms.h"
#include "ControlRigSpaceChannelEditors.h"
#include "ISequencer.h"

#include "Constraints/ControlRigTransformableHandle.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "LevelSequence.h"
#include "MovieSceneToolHelpers.h"

#include "TransformConstraint.h"
#include "Algo/Copy.h"
#include "Algo/Unique.h"
#include "ConstraintChannel.h"

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

void FConstraintChannelHelper::AddChildTransformKey(
	const UTransformableHandle* InHandle,
	const FFrameNumber& InTime,
	const TSharedPtr<ISequencer>& InSequencer)
{
	if (const UTransformableComponentHandle* ComponentHandle = Cast<UTransformableComponentHandle>(InHandle))
	{
		AActor* Actor = ComponentHandle->Component->GetOwner();
		if (!Actor)
		{
			return;
		}

		const FTransform LocalTransform = ComponentHandle->GetLocalTransform();
		const FGuid Guid = InSequencer->GetHandleToObject(Actor,true);
		if (!Guid.IsValid())
		{
			return;
		}

		const UMovieScene3DTransformSection* TransformSection = FBakingHelper::GetTransformSection(InSequencer.Get(), Guid, LocalTransform);
		if (!TransformSection)
		{
			return;
		}

		FBakingHelper::AddTransformKeys(TransformSection, {InTime}, {LocalTransform}, EMovieSceneTransformChannel::AllTransform);
		return; 
	}
	
	const UTransformableControlHandle* ControlHandle = Cast<UTransformableControlHandle>(InHandle);
	if (ControlHandle && ControlHandle->IsValid())
	{
		const FTransform LocalTransform = ControlHandle->GetLocalTransform();
		
		static constexpr bool bNotify = true, bUndo = false, bFixEuler = true;
	
		FRigControlModifiedContext KeyframeContext;
		KeyframeContext.SetKey = EControlRigSetKey::Always;
		KeyframeContext.KeyMask = static_cast<uint32>(EControlRigContextChannelToKey::AllTransform);

		const FFrameRate TickResolution = InSequencer->GetFocusedTickResolution();
		KeyframeContext.LocalTime = TickResolution.AsSeconds(FFrameTime(InTime));
	
		ControlHandle->ControlRig->SetControlLocalTransform(ControlHandle->ControlName, LocalTransform, bNotify, KeyframeContext, bUndo, bFixEuler);
	}
}

void FConstraintChannelHelper::AddConstraintKey(UTickableTransformConstraint* InConstraint)
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

	const UTransformableControlHandle* ControlHandle = Cast<UTransformableControlHandle>(InConstraint->ChildTRSHandle);
	if (!ControlHandle)
	{
		return;
	}

	if (UMovieSceneControlRigParameterSection* ParamSection = GetControlSection(ControlHandle, Sequencer))
	{
		ParamSection->AddConstraintChannel(InConstraint, true);

		if (FConstraintAndActiveChannel* Channel = ParamSection->GetConstraintChannel(InConstraint->GetFName()))
		{
			ParamSection->Modify();

			const FFrameRate TickResolution = Sequencer->GetFocusedTickResolution();
			const FFrameTime FrameTime = Sequencer->GetLocalTime().ConvertTo(TickResolution);
			const FFrameNumber Time = FrameTime.GetFrame();

			bool Value = false;
			CanAddKey(Channel->ActiveChannel, Time, Value);
			
			TMovieSceneChannelData<bool> ChannelData = Channel->ActiveChannel.GetData();
			ChannelData.AddKey(Time, InConstraint->Active);

			// bool PrevActiveValue = false; Channel->ActiveChannel.Evaluate(Time, PrevActiveValue);
			// bool NextActiveValue = false; Channel->ActiveChannel.Evaluate(Time, PrevActiveValue);

					
			// int32 ExistingIndex = ChannelData.FindKey(Time);
			// if (ExistingIndex != INDEX_NONE)
			// {
			// 	Handle = ChannelInterface.GetHandle(ExistingIndex);
			// 	using namespace UE::MovieScene;
			// 	AssignValue(Channel, Handle, Forward<FMovieSceneControlRigSpaceBaseKey>(Value));
			// }
			// else
			// {
			// 	ExistingIndex = ChannelInterface.AddKey(Time, Forward<FMovieSceneControlRigSpaceBaseKey>(Value));
			// 	Handle = ChannelInterface.GetHandle(ExistingIndex);
			// }
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
	
	return FBakingHelper::GetTransformSection(InSequencer.Get(), Guid);
}

TArrayView<FMovieSceneFloatChannel*> FConstraintChannelHelper::GetTransformFloatChannels(
	const UTransformableHandle* InHandle,
	const TSharedPtr<ISequencer>& InSequencer)
{
	// InHandle and InSequencer are assumed to be valid at this stage so no need to check
	
	static const TArrayView<FMovieSceneFloatChannel*> EmptyChannelsView;
	
	if (const UTransformableControlHandle* ControlHandle = Cast<UTransformableControlHandle>(InHandle))
	{
		UMovieSceneControlRigParameterSection* Section = GetControlSection(ControlHandle, InSequencer);
		FChannelMapInfo* pChannelIndex = nullptr;
		FRigControlElement* ControlElement = nullptr;
		Tie(ControlElement, pChannelIndex) = FControlRigSpaceChannelHelpers::GetControlAndChannelInfo(
			ControlHandle->ControlRig.Get(),
			Section,
			ControlHandle->ControlName);
    
		if (!pChannelIndex || !ControlElement)
		{
			return EmptyChannelsView;
		}

		// get the number of float channels to treat
		const int32 NumChannels = FControlRigSpaceChannelHelpers::GetNumFloatChannels(ControlElement->Settings.ControlType);
		if (!NumChannels)
		{
			return EmptyChannelsView;
		}

		// return a sub view that just represents the control's channels
		const TArrayView<FMovieSceneFloatChannel*> FloatChannels = Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
		const int32 ChannelStartIndex = pChannelIndex->ChannelIndex;
		return FloatChannels.Slice(ChannelStartIndex, NumChannels);
	}
	return EmptyChannelsView;
}

TArrayView<FMovieSceneDoubleChannel*> FConstraintChannelHelper::GetTransformDoubleChannels(
	const UTransformableHandle* InHandle,
	const TSharedPtr<ISequencer>& InSequencer)
{
	// InHandle and InSequencer are assumed to be valid at this stage so no need to check

	static const TArrayView<FMovieSceneDoubleChannel*> EmptyChannelsView;
	if (const UTransformableComponentHandle* ComponentHandle = Cast<UTransformableComponentHandle>(InHandle))
	{
		const UMovieScene3DTransformSection* Section = GetTransformSection(ComponentHandle, InSequencer);
		return Section ? Section->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>() : EmptyChannelsView;
	}

	return EmptyChannelsView;
}

template<typename ChannelType>
void GetFramesToCompensate(
	const FMovieSceneConstraintChannel& InActiveChannel,
	const bool InActiveValueToBeSet,
	const FFrameNumber& InTime,
	const TArrayView<ChannelType*>& InChannels,
	TArray<FFrameNumber>& OutFramesAfter)
{
	using ChannelValueType = typename ChannelType::ChannelValueType;

	const bool bHasKeys = (InActiveChannel.GetNumKeys() > 0);
	
	OutFramesAfter.Reset();

	// add the current frame
	OutFramesAfter.Add(InTime);

	// add the next frames that need transform compensation 
	for (const ChannelType* InChannel: InChannels)
	{
		const TMovieSceneChannelData<const ChannelValueType> ChannelData = InChannel->GetData();
		const TArrayView<const FFrameNumber> Times = ChannelData.GetTimes();
		if (!Times.IsEmpty())
		{
			// look for the first next key frame for this channel 
			const int32 NextTimeIndex = Algo::UpperBound(Times, InTime);
			if (Times.IsValidIndex(NextTimeIndex))
			{
				// store the time while the state is different
				for (int32 Index = NextTimeIndex; Index < Times.Num(); ++Index)
				{
					if (!bHasKeys)
					{
						OutFramesAfter.Add(Times[Index]);
					}
					else
					{
						bool NextValue = false; InActiveChannel.Evaluate(Times[Index], NextValue);
						if (NextValue == InActiveValueToBeSet)
						{
							break;
						}
						OutFramesAfter.Add(Times[Index]);
					}
				}
			}
		}
	}

	// uniqueness
	OutFramesAfter.Sort();
	OutFramesAfter.SetNum(Algo::Unique(OutFramesAfter));
}
namespace
{

struct FCompensationEvaluator
{
public:
	TArray<FTransform> ChildLocals;
	TArray<FTransform> ChildGlobals;
	TArray<FTransform> SpaceGlobals;
	
	FCompensationEvaluator(UTickableTransformConstraint* InConstraint)
		: Constraint(InConstraint)
		, Handle(InConstraint ? InConstraint->ChildTRSHandle : nullptr)
	{}

	void ComputeLocalTransforms(
		UWorld* InWorld, const TSharedPtr<ISequencer>& InSequencer,
		const TArray<FFrameNumber>& InFrames, const bool bToActive)
	{
		if (InFrames.IsEmpty())
		{
			return;
		}
		
		const TArray<UTickableTransformConstraint*> Constraints = GetHandleTransformConstraints(InWorld);
		if (Constraints.IsEmpty())
		{
			return;
		}

		// find last active constraint in the list that is different than the on we want to compensate for
		auto GetLastActiveConstraint = [this, Constraints]()
		{
			// find last active constraint in the list that is different than the on we want to compensate for
			const int32 LastActiveIndex = Constraints.FindLastByPredicate([this](const UTickableTransformConstraint* InConstraint)
			{
				return InConstraint->Active && InConstraint->bDynamicOffset && InConstraint != Constraint;
			});

			// if found, return its parent global transform
			return LastActiveIndex > INDEX_NONE ?Constraints[LastActiveIndex] : nullptr;
		};
		
		const UMovieScene* MovieScene = InSequencer->GetFocusedMovieSceneSequence()->GetMovieScene();
        const FFrameRate TickResolution = MovieScene->GetTickResolution();
        const EMovieScenePlayerStatus::Type PlaybackStatus = InSequencer->GetPlaybackStatus();

		const int32 NumFrames = InFrames.Num();

		// resize arrays to num frames + 1 as we also evaluate at InFrames[0]-1
		ChildLocals.SetNum(NumFrames+1);
        ChildGlobals.SetNum(NumFrames+1);
		SpaceGlobals.SetNum(NumFrames+1);

		const ETransformConstraintType ConstraintType = static_cast<ETransformConstraintType>(Constraint->GetType());
		
        for (int32 Index = 0; Index < NumFrames+1; ++Index)
        {
        	const FFrameNumber FrameNumber = (Index == 0) ? InFrames[0]-1 : InFrames[Index-1];
        
        	// evaluate animation
        	const FMovieSceneEvaluationRange EvaluationRange = FMovieSceneEvaluationRange(FFrameTime(FrameNumber), TickResolution);
        	const FMovieSceneContext Context = FMovieSceneContext(EvaluationRange, PlaybackStatus).SetHasJumped(true);
    
        	InSequencer->GetEvaluationTemplate().Evaluate(Context, *InSequencer);
        
        	// evaluate constraints
        	for (const UTickableTransformConstraint* InConstraint: Constraints)
        	{
        		InConstraint->Evaluate();
        	}
        
        	// evaluate ControlRig?
        	// ControlRig->Evaluate_AnyThread();

        	FTransform& ChildLocal = ChildLocals[Index];
        	FTransform& ChildGlobal = ChildGlobals[Index];
        	FTransform& SpaceGlobal = SpaceGlobals[Index];

        	// store child transforms        	
        	ChildLocal = Handle->GetLocalTransform();
        	ChildGlobal = Handle->GetGlobalTransform();
        	
        	const UTickableTransformConstraint* LastConstraint = GetLastActiveConstraint();
        	const ETransformConstraintType LastConstraintType = LastConstraint ?
				static_cast<ETransformConstraintType>(LastConstraint->GetType()) : ETransformConstraintType::Parent; 

        	// store constraint/parent space global transform
        	if (bToActive)
       		{
        		// if activating the constraint, store last constraint or parent space at T[0]-1
        		// and constraint space for all other times
				if (Index == 0)
				{
					if (LastConstraint)
					{
						SpaceGlobal = LastConstraint->GetParentGlobalTransform();
						ChildLocal = FTransformConstraintUtils::ComputeRelativeTransform(
							ChildLocal, ChildGlobal, SpaceGlobal, LastConstraintType);
					}
				}
				else
				{
					SpaceGlobal = Constraint->GetParentGlobalTransform();
					ChildLocal = FTransformConstraintUtils::ComputeRelativeTransform(
						ChildLocal, ChildGlobal, SpaceGlobal, ConstraintType);
				}
       		}
            else
            {
            	// if deactivating the constraint, store constraint space at T[0]-1
            	// and last constraint or parent space for all other times
            	if (Index == 0)
            	{
            		SpaceGlobal = Constraint->GetParentGlobalTransform();
            		ChildLocal = FTransformConstraintUtils::ComputeRelativeTransform(
            			ChildLocal, ChildGlobal, SpaceGlobal, ConstraintType);
            	}
                else
                {
                	if (LastConstraint)
                	{
                		SpaceGlobal = LastConstraint->GetParentGlobalTransform();
                		ChildLocal = FTransformConstraintUtils::ComputeRelativeTransform(
                			ChildLocal, ChildGlobal, SpaceGlobal, LastConstraintType);
                	}
                }
            }
        }
	}

	void ComputeCompensation(UWorld* InWorld, const TSharedPtr<ISequencer>& InSequencer, const FFrameNumber& InTime)
	{
		const TArray<UTickableTransformConstraint*> Constraints = GetHandleTransformConstraints(InWorld);
		if (Constraints.IsEmpty())
		{
			return;
		}

		// find last active constraint in the list that is different than the on we want to compensate for
		auto GetLastActiveConstraint = [this, Constraints]()
		{
			// find last active constraint in the list that is different than the on we want to compensate for
			const int32 LastActiveIndex = Constraints.FindLastByPredicate([this](const UTickableTransformConstraint* InConstraint)
			{
				return InConstraint->Active && InConstraint->bDynamicOffset;
			});

			// if found, return its parent global transform
			return LastActiveIndex > INDEX_NONE ? Constraints[LastActiveIndex] : nullptr;
		};

		auto EvaluateAt = [InSequencer, &Constraints](const FFrameNumber InFrame)
		{
			const UMovieScene* MovieScene = InSequencer->GetFocusedMovieSceneSequence()->GetMovieScene();
			const FFrameRate TickResolution = MovieScene->GetTickResolution();
			const EMovieScenePlayerStatus::Type PlaybackStatus = InSequencer->GetPlaybackStatus();

			const FMovieSceneEvaluationRange EvaluationRange0 = FMovieSceneEvaluationRange(FFrameTime(InFrame), TickResolution);
			const FMovieSceneContext Context0 = FMovieSceneContext(EvaluationRange0, PlaybackStatus).SetHasJumped(true);

			InSequencer->GetEvaluationTemplate().Evaluate(Context0, *InSequencer);

			for (const UTickableTransformConstraint* InConstraint: Constraints)
			{
				InConstraint->Evaluate();
			}

			// ControlRig->Evaluate_AnyThread();
		};

		// allocate
		ChildLocals.SetNum(1);
		ChildGlobals.SetNum(1);
		SpaceGlobals.SetNum(1);
		
		// evaluate at InTime and store global
		EvaluateAt(InTime);
		ChildGlobals[0] = Handle->GetGlobalTransform();

		// evaluate at InTime-1 and store local
		EvaluateAt(InTime-1);
        ChildLocals[0] = Handle->GetLocalTransform();

		// if constraint at T-1 then switch to its space
        if (const UTickableTransformConstraint* LastConstraint = GetLastActiveConstraint())
        {
        	SpaceGlobals[0] = LastConstraint->GetParentGlobalTransform();
        	
        	const ETransformConstraintType LastConstraintType = static_cast<ETransformConstraintType>(LastConstraint->GetType());
        	ChildLocals[0] = FTransformConstraintUtils::ComputeRelativeTransform(ChildLocals[0],
		ChildGlobals[0], SpaceGlobals[0], LastConstraintType);
        }
        else // switch to parent space
        {
        	const FTransform ChildLocal = ChildLocals[0];
        	Handle->SetGlobalTransform(ChildGlobals[0]);
        	ChildLocals[0] = Handle->GetLocalTransform();
        	Handle->SetLocalTransform(ChildLocal);
        }
	}
	
private:

	TArray<UTickableTransformConstraint*> GetHandleTransformConstraints(UWorld* InWorld) const
	{
		TArray<UTickableTransformConstraint*> TransformConstraints;
		if (Handle)
		{
			// get sorted transform constraints
			const FConstraintsManagerController& Controller = FConstraintsManagerController::Get(InWorld);
			static constexpr bool bSorted = true;
			TArray<TObjectPtr<UTickableConstraint>> Constraints = Controller.GetParentConstraints(Handle->GetHash(), bSorted);
			for (const TObjectPtr<UTickableConstraint>& InConstraint: Constraints)
			{
				if (UTickableTransformConstraint* TransformConstraint = Cast<UTickableTransformConstraint>(InConstraint.Get()))
				{
					TransformConstraints.Add(TransformConstraint);
				}
			}
		}
		return TransformConstraints;
	}
	
	UTickableTransformConstraint* Constraint = nullptr;
	UTransformableHandle* Handle = nullptr;
};
	
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
		Section->AddConstraintChannel(InConstraint, true);

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
				const TArrayView<FMovieSceneFloatChannel*> Channels = GetTransformFloatChannels(ControlHandle, InSequencer);
				TArray<FFrameNumber> FramesToCompensate;
				GetFramesToCompensate<FMovieSceneFloatChannel>(Channel->ActiveChannel, ActiveValueToBeSet, Time, Channels, FramesToCompensate);

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
						FControlRigSpaceChannelHelpers::EvaluateTangentAtThisTime(ChannelIndex, NumChannels, true,Section, Time, Tangents);
					}
				}
				

				const EMovieSceneTransformChannel ChannelsToKey = FConstraintBaker::GetChannelsToKey(InConstraint);
				
				// add child's transform key at Time-1 to keep animation
				{
					const FFrameNumber TimeMinusOne(Time - 1);

					FBakingHelper::AddTransformKeys(ControlRig, ControlName, { TimeMinusOne },
						{ ChildLocals[0] }, ChannelsToKey, TickResolution, true);

					// set tangents at Time-1
					if (NumChannels > 0)
					{
						FControlRigSpaceChannelHelpers::SetTangentsAtThisTime(ChannelIndex, NumChannels, true, Section, TimeMinusOne, Tangents);
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
					FBakingHelper::AddTransformKeys(ControlRig, ControlHandle->ControlName, FramesToCompensate,
						ChildLocals, ChannelsToKey, TickResolution, true);

					// set tangents at Time
					if (NumChannels > 0 )
					{
						FControlRigSpaceChannelHelpers::SetTangentsAtThisTime(ChannelIndex, NumChannels, true, Section, Time, Tangents);
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

	if (UMovieScene3DTransformSection* Section = FBakingHelper::GetTransformSection(InSequencer.Get(), Guid, LocalTransform))
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
				const TArrayView<FMovieSceneDoubleChannel*> Channels = GetTransformDoubleChannels(ComponentHandle, InSequencer);
				TArray<FFrameNumber> FramesToCompensate;
				GetFramesToCompensate<FMovieSceneDoubleChannel>(Channel->ActiveChannel, ActiveValueToBeSet, Time, Channels, FramesToCompensate);

				// store child and space transforms for these frames
				FCompensationEvaluator Evaluator(InConstraint);
				Evaluator.ComputeLocalTransforms(Actor->GetWorld(), InSequencer, FramesToCompensate, ActiveValueToBeSet);
				TArray<FTransform>& ChildLocals = Evaluator.ChildLocals;

				// store tangents at this time
				TArray<FMovieSceneTangentData> Tangents;
				const int32 ChannelIndex = 0;
				const int32 NumChannels = 9;
				FControlRigSpaceChannelHelpers::EvaluateTangentAtThisTime(ChannelIndex, NumChannels, false, Section, Time, Tangents);

				const EMovieSceneTransformChannel ChannelsToKey = FConstraintBaker::GetChannelsToKey(InConstraint);

				// add child's transform key at Time-1 to keep animation
				{
					const FFrameNumber TimeMinusOne(Time - 1);

					FBakingHelper::AddTransformKeys(Section, { TimeMinusOne }, { ChildLocals[0] }, ChannelsToKey);

					FControlRigSpaceChannelHelpers::SetTangentsAtThisTime(ChannelIndex, NumChannels, false,Section, TimeMinusOne, Tangents);
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
					FBakingHelper::AddTransformKeys(Section, FramesToCompensate, ChildLocals, ChannelsToKey);

					// set tangents at Time
					FControlRigSpaceChannelHelpers::SetTangentsAtThisTime(ChannelIndex,NumChannels, false, Section, Time, Tangents);
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
	for (const FConstraintAndActiveChannel& Channel: TransformConstraintsChannels)
	{
		const TArrayView<const FFrameNumber> FramesToCompensate = GetSpaceTimesToCompensate(Channel);
		for (const FFrameNumber& Time : FramesToCompensate)
		{
			const FFrameNumber TimeMinusOne(Time-1);
			
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
				
				const EMovieSceneTransformChannel ChannelsToKey = FConstraintBaker::GetChannelsToKey(Constraint);
				FConstraintBaker::AddTransformKeys(
					InSequencer, Constraint->ChildTRSHandle, {TimeMinusOne}, LocalTransforms, ChannelsToKey);
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
