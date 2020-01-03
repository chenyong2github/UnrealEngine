// Copyright Epic Games, Inc. All Rights Reserved.

#include "Evaluation/MovieSceneEventTemplate.h"
#include "Tracks/MovieSceneEventTrack.h"
#include "MovieSceneSequence.h"
#include "Evaluation/MovieSceneEvaluationTemplateInstance.h"
#include "EngineGlobals.h"
#include "MovieScene.h"
#include "Evaluation/MovieSceneEvaluation.h"
#include "IMovieScenePlayer.h"
#include "Algo/Accumulate.h"
#include "Engine/LevelScriptActor.h"
#include "Logging/TokenizedMessage.h"
#include "Logging/MessageLog.h"
#include "Misc/UObjectToken.h"

#define LOCTEXT_NAMESPACE "MovieSceneEventTemplate"

DECLARE_CYCLE_STAT(TEXT("Event Track Token Execute"), MovieSceneEval_EventTrack_TokenExecute, STATGROUP_MovieSceneEval);

struct FMovieSceneEventData
{
	FMovieSceneEventData(const FEventPayload& InPayload, float InGlobalPosition) : Payload(InPayload), GlobalPosition(InGlobalPosition) {}

	FEventPayload Payload;
	float GlobalPosition;
};

/** A movie scene execution token that stores a specific transform, and an operand */
struct FEventTrackExecutionToken
	: IMovieSceneExecutionToken
{
	FEventTrackExecutionToken(TArray<FMovieSceneEventData> InEvents, const TArray<FMovieSceneObjectBindingID>& InEventReceivers) : Events(MoveTemp(InEvents)), EventReceivers(InEventReceivers) {}

	/** Execute this token, operating on all objects referenced by 'Operand' */
	virtual void Execute(const FMovieSceneContext& Context, const FMovieSceneEvaluationOperand& Operand, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) override
	{
		MOVIESCENE_DETAILED_SCOPE_CYCLE_COUNTER(MovieSceneEval_EventTrack_TokenExecute)
		
		TArray<float> PerformanceCaptureEventPositions;

		// Resolve event contexts to trigger the event on
		TArray<UObject*> EventContexts;

		// If we have specified event receivers, use those
		if (EventReceivers.Num())
		{
			EventContexts.Reserve(EventReceivers.Num());
			for (FMovieSceneObjectBindingID ID : EventReceivers)
			{
				// Ensure that this ID is resolvable from the root, based on the current local sequence ID
				ID = ID.ResolveLocalToRoot(Operand.SequenceID, Player.GetEvaluationTemplate().GetHierarchy());

				// Lookup the object(s) specified by ID in the player
				for (TWeakObjectPtr<> WeakEventContext : Player.FindBoundObjects(ID.GetGuid(), ID.GetSequenceID()))
				{
					if (UObject* EventContext = WeakEventContext.Get())
					{
						EventContexts.Add(EventContext);
					}
				}
			}
		}
		else
		{
			// If we haven't specified event receivers, use the default set defined on the player
			EventContexts = Player.GetEventContexts();
		}

		for (UObject* EventContextObject : EventContexts)
		{
			if (!EventContextObject)
			{
				continue;
			}

			for (FMovieSceneEventData& Event : Events)
			{
#if !UE_BUILD_SHIPPING
				if (Event.Payload.EventName == NAME_PerformanceCapture)
				{
					PerformanceCaptureEventPositions.Add(Event.GlobalPosition);
				}
#endif
				TriggerEvent(Event, *EventContextObject, Player);
			}
		}

#if !UE_BUILD_SHIPPING
		if (PerformanceCaptureEventPositions.Num())
		{
			UObject* PlaybackContext = Player.GetPlaybackContext();
			UWorld* World = PlaybackContext ? PlaybackContext->GetWorld() : nullptr;
			if (World)
			{
				FString LevelSequenceName = Player.GetEvaluationTemplate().GetSequence(MovieSceneSequenceID::Root)->GetName();
			
				for (float EventPosition : PerformanceCaptureEventPositions)
				{
					GEngine->PerformanceCapture(World, World->GetName(), LevelSequenceName, EventPosition);
				}
			}
		}
#endif	// UE_BUILD_SHIPPING
	}

	void TriggerEvent(FMovieSceneEventData& Event, UObject& EventContextObject, IMovieScenePlayer& Player)
	{
		UFunction* EventFunction = EventContextObject.FindFunction(Event.Payload.EventName);

		if (EventFunction == nullptr)
		{
			// Don't want to log out a warning for every event context.
			return;
		}
		else
		{
			FStructOnScope ParameterStruct(nullptr);
			Event.Payload.Parameters.GetInstance(ParameterStruct);

			uint8* Parameters = ParameterStruct.GetStructMemory();

			const UStruct* Struct = ParameterStruct.GetStruct();
			if (EventFunction->ReturnValueOffset != MAX_uint16)
			{
				UE_LOG(LogMovieScene, Warning, TEXT("Sequencer Event Track: Cannot trigger events that return values (for event '%s')."), *Event.Payload.EventName.ToString());
				return;
			}
			else
			{
				TFieldIterator<FProperty> ParamIt(EventFunction);
				TFieldIterator<FProperty> ParamInstanceIt(Struct);
				for (int32 NumParams = 0; ParamIt || ParamInstanceIt; ++NumParams, ++ParamIt, ++ParamInstanceIt)
				{
					if (!ParamInstanceIt)
					{
						UE_LOG(LogMovieScene, Warning, TEXT("Sequencer Event Track: Parameter count mismatch for event '%s'. Required parameter of type '%s' at index '%d'."), *Event.Payload.EventName.ToString(), *ParamIt->GetName(), NumParams);
						return;
					}
					else if (!ParamIt)
					{
						// Mismatch (too many params)
						UE_LOG(LogMovieScene, Warning, TEXT("Sequencer Event Track: Parameter count mismatch for event '%s'. Parameter struct contains too many parameters ('%s' is superfluous at index '%d'."), *Event.Payload.EventName.ToString(), *ParamInstanceIt->GetName(), NumParams);
						return;
					}
					else if (!ParamInstanceIt->SameType(*ParamIt) || ParamInstanceIt->GetOffset_ForUFunction() != ParamIt->GetOffset_ForUFunction() || ParamInstanceIt->GetSize() != ParamIt->GetSize())
					{
						UE_LOG(LogMovieScene, Warning, TEXT("Sequencer Event Track: Parameter type mismatch for event '%s' ('%s' != '%s')."),
							*Event.Payload.EventName.ToString(),
							*ParamInstanceIt->GetClass()->GetName(),
							*ParamIt->GetClass()->GetName()
						);
						return;
					}
				}
			}

			// Technically, anything bound to the event could mutate the parameter payload,
			// but we're going to treat that as misuse, rather than copy the parameters each time
			EventContextObject.ProcessEvent(EventFunction, Parameters);
		}
	}

	TArray<FMovieSceneEventData> Events;
	TArray<FMovieSceneObjectBindingID, TInlineAllocator<2>> EventReceivers;
};

struct FEventTriggerExecutionToken
	: IMovieSceneExecutionToken
{
	FEventTriggerExecutionToken(TArray<FMovieSceneEventPtrs> InEvents, const TArray<FMovieSceneObjectBindingID>& InEventReceivers)
		: Events(MoveTemp(InEvents)), EventReceivers(InEventReceivers)
	{}

	/** Execute this token, operating on all objects referenced by 'Operand' */
	virtual void Execute(const FMovieSceneContext& Context, const FMovieSceneEvaluationOperand& Operand, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) override
	{
		MOVIESCENE_DETAILED_SCOPE_CYCLE_COUNTER(MovieSceneEval_EventTrack_TokenExecute)

		UObject* DirectorInstance = Player.GetEvaluationTemplate().GetOrCreateDirectorInstance(Operand.SequenceID, Player);
		if (!DirectorInstance)
		{
#if !NO_LOGGING
			UE_LOG(LogMovieScene, Warning, TEXT("Failed to trigger the following events because no director instance was available: %s."), *GenerateEventListString());
#endif
			return;
		}

		// Resolve event contexts to trigger the event on
		TArray<UObject*> EventContexts;

		// If the event track resides within an object binding, add those to the event contexts
		if (Operand.ObjectBindingID.IsValid())
		{
			for (TWeakObjectPtr<> WeakEventContext : Player.FindBoundObjects(Operand))
			{
				if (UObject* EventContext = WeakEventContext.Get())
				{
					EventContexts.Add(EventContext);
				}
			}
		}

		// If we have specified event receivers
		if (EventReceivers.Num())
		{
			EventContexts.Reserve(EventReceivers.Num());
			for (FMovieSceneObjectBindingID ID : EventReceivers)
			{
				// Ensure that this ID is resolvable from the root, based on the current local sequence ID
				ID = ID.ResolveLocalToRoot(Operand.SequenceID, Player.GetEvaluationTemplate().GetHierarchy());

				// Lookup the object(s) specified by ID in the player
				for (TWeakObjectPtr<> WeakEventContext : Player.FindBoundObjects(ID.GetGuid(), ID.GetSequenceID()))
				{
					if (UObject* EventContext = WeakEventContext.Get())
					{
						EventContexts.Add(EventContext);
					}
				}
			}
		}

		// If we haven't specified event receivers, use the default set defined on the player		
		if (EventContexts.Num() == 0)
		{
			EventContexts = Player.GetEventContexts();
		}

#if WITH_EDITOR
		const static FName NAME_CallInEditor(TEXT("CallInEditor"));

		UWorld* World = DirectorInstance->GetWorld();
		bool bIsGameWorld = World && World->IsGameWorld();
#endif // WITH_EDITOR

		for (const FMovieSceneEventPtrs& Event : Events)
		{

#if WITH_EDITOR
			if (!bIsGameWorld && !Event.Function->HasMetaData(NAME_CallInEditor))
			{
				UE_LOG(LogMovieScene, Verbose, TEXT("Refusing to trigger event '%s' in editor world when 'Call in Editor' is false."), *Event.Function->GetName());
				continue;
			}
#endif // WITH_EDITOR


			UE_LOG(LogMovieScene, VeryVerbose, TEXT("Triggering event '%s'."), *Event.Function->GetName());

			if (Event.Function->NumParms == 0)
			{
				DirectorInstance->ProcessEvent(Event.Function, nullptr);
			}
			else
			{
				TriggerEventWithParameters(DirectorInstance, Event, EventContexts, Player, Operand.SequenceID);
			}
		}
			}

	void TriggerEventWithParameters(UObject* DirectorInstance, const FMovieSceneEventPtrs& Event, TArrayView<UObject* const> EventContexts, IMovieScenePlayer& Player, FMovieSceneSequenceID SequenceID)
	{
		if (!ensureMsgf(!Event.BoundObjectProperty.Get() || (Event.BoundObjectProperty->GetOwner<UObject>() == Event.Function && Event.BoundObjectProperty->GetOffset_ForUFunction() < Event.Function->ParmsSize), TEXT("Bound object property belongs to the wrong function or has an offset greater than the parameter size! This should never happen and indicates a BP compilation or nativization error.")))
		{
			return;
		}

		// Parse all function parameters.
		uint8* Parameters = (uint8*)FMemory_Alloca(Event.Function->ParmsSize + Event.Function->MinAlignment);
		Parameters = Align(Parameters, Event.Function->MinAlignment);

		// Mem zero the parameter list
		FMemory::Memzero(Parameters, Event.Function->ParmsSize);

		// Initialize all CPF_Param properties - these are aways at the head of the list
		for (TFieldIterator<FProperty> It(Event.Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			FProperty* LocalProp = *It;
			checkSlow(LocalProp);
			if (!LocalProp->HasAnyPropertyFlags(CPF_ZeroConstructor))
			{
				LocalProp->InitializeValue_InContainer(Parameters);
			}
		}

		for (UObject* BoundObject : EventContexts)
		{
			int32 NumParmsPatched = 0;

			// Attempt to bind the object to the function parameters
			if (!PatchBoundObject(Parameters, BoundObject, Event.BoundObjectProperty.Get(), Player, SequenceID))
			{
				continue;
			}
			else
			{
				++NumParmsPatched;
			}

			ensureAlwaysMsgf(Event.Function->NumParms == NumParmsPatched, TEXT("Failed to patch the correct number of parameters for function call. Some parameters may be incorrect."));

			// Call the function
			DirectorInstance->ProcessEvent(Event.Function, Parameters);
		}

		// Destroy all parameter properties one by one
		for (TFieldIterator<FProperty> It(Event.Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->DestroyValue_InContainer(Parameters);
		}
	}

	bool PatchBoundObject(uint8* Parameters, UObject* BoundObject, FProperty* BoundObjectProperty, IMovieScenePlayer& Player, FMovieSceneSequenceID SequenceID)
	{
		if (!BoundObjectProperty)
		{
			return true;
		}

		if (FInterfaceProperty* InterfaceParameter = CastField<FInterfaceProperty>(BoundObjectProperty))
			{
			if (BoundObject->GetClass()->ImplementsInterface(InterfaceParameter->InterfaceClass))
			{
				FScriptInterface Interface;
				Interface.SetObject(BoundObject);
				Interface.SetInterface(BoundObject->GetInterfaceAddress(InterfaceParameter->InterfaceClass));
				InterfaceParameter->SetPropertyValue_InContainer(Parameters, Interface);
				return true;
			}

			FMessageLog("PIE").Warning()
				->AddToken(FUObjectToken::Create(BoundObjectProperty->GetOwnerUObject()))
				->AddToken(FUObjectToken::Create(Player.GetEvaluationTemplate().GetSequence(SequenceID)))
				->AddToken(FTextToken::Create(FText::Format(LOCTEXT("LevelBP_InterfaceNotImplemented_Error", "Failed to trigger event because it does not implement the necessary interface. Function expects a '{0}'."), FText::FromName(InterfaceParameter->InterfaceClass->GetFName()))));
			return false;
		}

		if (FObjectProperty* ObjectParameter = CastField<FObjectProperty>(BoundObjectProperty))
		{
			if (BoundObject->IsA<ALevelScriptActor>())
			{
				FMessageLog("PIE").Warning()
					->AddToken(FUObjectToken::Create(BoundObjectProperty->GetOwnerUObject()))
					->AddToken(FUObjectToken::Create(Player.GetEvaluationTemplate().GetSequence(SequenceID)))
					->AddToken(FTextToken::Create(LOCTEXT("LevelBP_LevelScriptActor_Error", "Failed to trigger event: only Interface pins are supported for master tracks within Level Sequences. Please remove the pin, or change it to an interface that is implemented on the desired level blueprint.")));

				return false;
			}
			else if (!BoundObject->IsA(ObjectParameter->PropertyClass))
			{
				FMessageLog("PIE").Warning()
					->AddToken(FUObjectToken::Create(Player.GetEvaluationTemplate().GetSequence(SequenceID)))
					->AddToken(FUObjectToken::Create(BoundObjectProperty->GetOwnerUObject()))
					->AddToken(FUObjectToken::Create(BoundObject))
					->AddToken(FTextToken::Create(FText::Format(LOCTEXT("LevelBP_InvalidCast_Error", "Failed to trigger event: Cast to {0} failed."), FText::FromName(ObjectParameter->PropertyClass->GetFName()))));

				return false;
			}

			ObjectParameter->SetObjectPropertyValue_InContainer(Parameters, BoundObject);
			return true;
		}

		FMessageLog("PIE").Warning()
			->AddToken(FUObjectToken::Create(Player.GetEvaluationTemplate().GetSequence(SequenceID)))
			->AddToken(FUObjectToken::Create(BoundObjectProperty->GetOwnerUObject()))
			->AddToken(FUObjectToken::Create(BoundObject))
			->AddToken(FTextToken::Create(FText::Format(LOCTEXT("LevelBP_UnsupportedProperty_Error", "Failed to trigger event: Unsupported property type for bound object: {0}."), FText::FromName(BoundObjectProperty->GetClass()->GetFName()))));
		return false;
	}

#if !NO_LOGGING
	FString GenerateEventListString() const
	{
		return Algo::Accumulate(Events, FString(), [](FString&& InString, const FMovieSceneEventPtrs& EventPtrs){
			if (InString.Len() > 0)
			{
				InString += TEXT(", ");
			}
			return InString + EventPtrs.Function->GetName();
		});
	}
#endif

	TArray<FMovieSceneEventPtrs> Events;
	TArray<FMovieSceneObjectBindingID, TInlineAllocator<2>> EventReceivers;
};


FMovieSceneEventTemplateBase::FMovieSceneEventTemplateBase(const UMovieSceneEventTrack& Track)
	: EventReceivers(Track.EventReceivers)
	, bFireEventsWhenForwards(Track.bFireEventsWhenForwards)
	, bFireEventsWhenBackwards(Track.bFireEventsWhenBackwards)
{
}

FMovieSceneEventSectionTemplate::FMovieSceneEventSectionTemplate(const UMovieSceneEventSection& Section, const UMovieSceneEventTrack& Track)
	: FMovieSceneEventTemplateBase(Track)
	, EventData(Section.GetEventData())
{
}

void FMovieSceneEventSectionTemplate::EvaluateSwept(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const TRange<FFrameNumber>& SweptRange, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const
{
	// Don't allow events to fire when playback is in a stopped state. This can occur when stopping 
	// playback and returning the current position to the start of playback. It's not desireable to have 
	// all the events from the last playback position to the start of playback be fired.
	if (Context.GetStatus() == EMovieScenePlayerStatus::Stopped || Context.IsSilent())
	{
		return;
	}

	const bool bBackwards = Context.GetDirection() == EPlayDirection::Backwards;

	if ((!bBackwards && !bFireEventsWhenForwards) ||
		(bBackwards && !bFireEventsWhenBackwards))
	{
		return;
	}

	TArray<FMovieSceneEventData> Events;

	TArrayView<const FFrameNumber>  KeyTimes   = EventData.GetKeyTimes();
	TArrayView<const FEventPayload> KeyValues  = EventData.GetKeyValues();

	const int32 First = bBackwards ? KeyTimes.Num() - 1 : 0;
	const int32 Last = bBackwards ? 0 : KeyTimes.Num() - 1;
	const int32 Inc = bBackwards ? -1 : 1;

	const float PositionInSeconds = Context.GetTime() * Context.GetRootToSequenceTransform().Inverse() / Context.GetFrameRate();

	if (bBackwards)
	{
		// Trigger events backwards
		for (int32 KeyIndex = KeyTimes.Num() - 1; KeyIndex >= 0; --KeyIndex)
		{
			FFrameNumber Time = KeyTimes[KeyIndex];
			if (SweptRange.Contains(Time))
			{
				Events.Add(FMovieSceneEventData(KeyValues[KeyIndex], PositionInSeconds));
			}
		}
	}
	// Trigger events forwards
	else for (int32 KeyIndex = 0; KeyIndex < KeyTimes.Num(); ++KeyIndex)
	{
		FFrameNumber Time = KeyTimes[KeyIndex];
		if (SweptRange.Contains(Time))
		{
			Events.Add(FMovieSceneEventData(KeyValues[KeyIndex], PositionInSeconds));
		}
	}


	if (Events.Num())
	{
		ExecutionTokens.Add(FEventTrackExecutionToken(MoveTemp(Events), EventReceivers));
	}
}



FMovieSceneEventTriggerTemplate::FMovieSceneEventTriggerTemplate(const UMovieSceneEventTriggerSection& Section, const UMovieSceneEventTrack& Track)
	: FMovieSceneEventTemplateBase(Track)
{
	TMovieSceneChannelData<const FMovieSceneEvent> EventData = Section.EventChannel.GetData();
	TArrayView<const FFrameNumber>     Times  = EventData.GetTimes();
	TArrayView<const FMovieSceneEvent>             EntryPoints = EventData.GetValues();

	EventTimes.Reserve(Times.Num());
	Events.Reserve(Times.Num());

	for (int32 Index = 0; Index < Times.Num(); ++Index)
	{
		EventTimes.Add(Times[Index]);
		Events.Add(EntryPoints[Index].Ptrs);
	}
}

void FMovieSceneEventTriggerTemplate::EvaluateSwept(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const TRange<FFrameNumber>& SweptRange, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const
{
	// Don't allow events to fire when playback is in a stopped state. This can occur when stopping 
	// playback and returning the current position to the start of playback. It's not desireable to have 
	// all the events from the last playback position to the start of playback be fired.
	if (Context.GetStatus() == EMovieScenePlayerStatus::Stopped || Context.IsSilent())
	{
		return;
	}

	const bool bBackwards = Context.GetDirection() == EPlayDirection::Backwards;

	if ((!bBackwards && !bFireEventsWhenForwards) ||
		(bBackwards && !bFireEventsWhenBackwards))
	{
		return;
	}

	TArray<FMovieSceneEventPtrs> EventsToTrigger;

	const int32 First = bBackwards ? EventTimes.Num() - 1 : 0;
	const int32 Last = bBackwards ? 0 : EventTimes.Num() - 1;
	const int32 Inc = bBackwards ? -1 : 1;

	const float PositionInSeconds = Context.GetTime() * Context.GetRootToSequenceTransform().Inverse() / Context.GetFrameRate();

	if (bBackwards)
	{
		// Trigger events backwards
		for (int32 KeyIndex = EventTimes.Num() - 1; KeyIndex >= 0; --KeyIndex)
		{
			FFrameNumber Time = EventTimes[KeyIndex];
			if (Events[KeyIndex].Function && SweptRange.Contains(Time))
			{
				EventsToTrigger.Add(Events[KeyIndex]);
			}
		}
	}
	// Trigger events forwards
	else for (int32 KeyIndex = 0; KeyIndex < EventTimes.Num(); ++KeyIndex)
	{
		FFrameNumber Time = EventTimes[KeyIndex];
		if (Events[KeyIndex].Function && SweptRange.Contains(Time))
		{
			EventsToTrigger.Add(Events[KeyIndex]);
		}
	}


	if (EventsToTrigger.Num())
	{
		ExecutionTokens.Add(FEventTriggerExecutionToken(MoveTemp(EventsToTrigger), EventReceivers));
	}
}



FMovieSceneEventRepeaterTemplate::FMovieSceneEventRepeaterTemplate(const UMovieSceneEventRepeaterSection& Section, const UMovieSceneEventTrack& Track)
	: FMovieSceneEventTemplateBase(Track)
	, EventToTrigger(Section.Event.Ptrs)
{
}

void FMovieSceneEventRepeaterTemplate::EvaluateSwept(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const TRange<FFrameNumber>& SweptRange, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const
{
	const bool bBackwards = Context.GetDirection() == EPlayDirection::Backwards;
	FFrameNumber CurrentFrame = bBackwards ? Context.GetTime().CeilToFrame() : Context.GetTime().FloorToFrame();

	// Don't allow events to fire when playback is in a stopped state. This can occur when stopping 
	// playback and returning the current position to the start of playback. It's not desireable to have 
	// all the events from the last playback position to the start of playback be fired.
	if (!EventToTrigger.Function || !SweptRange.Contains(CurrentFrame) || Context.GetStatus() == EMovieScenePlayerStatus::Stopped || Context.IsSilent())
	{
		return;
	}

	
	if ((!bBackwards && bFireEventsWhenForwards) || (bBackwards && bFireEventsWhenBackwards))
	{
		TArray<FMovieSceneEventPtrs> Events = { EventToTrigger };
		ExecutionTokens.Add(FEventTriggerExecutionToken(MoveTemp(Events), EventReceivers));
	}
}

#undef LOCTEXT_NAMESPACE