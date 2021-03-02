// Copyright Epic Games, Inc. All Rights Reserved.

#include "EnhancedInputSubsystemInterface.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputModule.h"
#include "EnhancedPlayerInput.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
#include "UObject/UObjectIterator.h"

/* Shared input subsystem functionality.
 * See EnhancedInputSubsystemInterfaceDebug.cpp for debug specific functionality.
 */

static constexpr int32 GGlobalAxisConfigMode_Default = 0;
static constexpr int32 GGlobalAxisConfigMode_All = 1;
static constexpr int32 GGlobalAxisConfigMode_None = 2;

static int32 GGlobalAxisConfigMode = 0;
static FAutoConsoleVariableRef GCVarGlobalAxisConfigMode(
	TEXT("input.GlobalAxisConfigMode"),
	GGlobalAxisConfigMode,
	TEXT("Whether or not to apply Global Axis Config settings. 0 = Default (Mouse Only), 1 = All, 2 = None")
);

void IEnhancedInputSubsystemInterface::ClearAllMappings()
{
	if (UEnhancedPlayerInput* PlayerInput = GetPlayerInput())
	{
		PlayerInput->AppliedInputContexts.Empty();
		RequestRebuildControlMappings(false);
	}
}

void IEnhancedInputSubsystemInterface::AddMappingContext(const UInputMappingContext* MappingContext, int32 Priority)
{
	// Layer mappings on top of existing mappings
	if (MappingContext)
	{
		if (UEnhancedPlayerInput* PlayerInput = GetPlayerInput())
		{
			PlayerInput->AppliedInputContexts.Add(MappingContext, Priority);
			RequestRebuildControlMappings(false);
		}
	}
}

void IEnhancedInputSubsystemInterface::RemoveMappingContext(const UInputMappingContext* MappingContext)
{
	if (MappingContext)
	{
		if (UEnhancedPlayerInput* PlayerInput = GetPlayerInput())
		{
			PlayerInput->AppliedInputContexts.Remove(MappingContext);
			RequestRebuildControlMappings(false);
		}
	}
}

void IEnhancedInputSubsystemInterface::RequestRebuildControlMappings(bool bForceImmediately)
{
	bMappingRebuildPending = true;
	if (bForceImmediately)
	{
		RebuildControlMappings();
	}
}


EMappingQueryResult IEnhancedInputSubsystemInterface::QueryMapKeyInActiveContextSet(const UInputMappingContext* InputContext, const UInputAction* Action, FKey Key, TArray<FMappingQueryIssue>& OutIssues, EMappingQueryIssue BlockingIssues/* = DefaultMappingIssues::StandardFatal*/)
{
	UEnhancedPlayerInput* PlayerInput = GetPlayerInput();
	if (!PlayerInput)
	{
		return EMappingQueryResult::Error_EnhancedInputNotEnabled;
	}

	// TODO: Inefficient, but somewhat forgivable as the mapping context count is likely to be single figure.
	TMap<const UInputMappingContext*, int32> OrderedInputContexts = PlayerInput->AppliedInputContexts;
	OrderedInputContexts.ValueSort([](const int32& A, const int32& B) { return A > B; });

	TArray<UInputMappingContext*> Applied;
	Applied.Reserve(OrderedInputContexts.Num());
	for (const TPair<const UInputMappingContext*, int32>& ContextPair : OrderedInputContexts)
	{
		Applied.Add(const_cast<UInputMappingContext*>(ContextPair.Key));
	}

	return QueryMapKeyInContextSet(Applied, InputContext, Action, Key, OutIssues, BlockingIssues);
}

EMappingQueryResult IEnhancedInputSubsystemInterface::QueryMapKeyInContextSet(const TArray<UInputMappingContext*>& PrioritizedActiveContexts, const UInputMappingContext* InputContext, const UInputAction* Action, FKey Key, TArray<FMappingQueryIssue>& OutIssues, EMappingQueryIssue BlockingIssues/* = DefaultMappingIssues::StandardFatal*/)
{
	if (!Action)
	{
		return EMappingQueryResult::Error_InvalidAction;
	}

	OutIssues.Reset();

	// Report on keys being bound that don't support the action's value type.
	EInputActionValueType KeyValueType = FInputActionValue(Key).GetValueType();
	if (Action->ValueType != KeyValueType)
	{
		// We exclude bool -> Axis1D promotions, as these are commonly used for paired mappings (e.g. W + S/Negate bound to a MoveForward action), and are fairly intuitive anyway.
		if (Action->ValueType != EInputActionValueType::Axis1D || KeyValueType != EInputActionValueType::Boolean)
		{
			OutIssues.Add(KeyValueType < Action->ValueType ? EMappingQueryIssue::ForcesTypePromotion : EMappingQueryIssue::ForcesTypeDemotion);
		}
	}

	enum class EStage : uint8
	{
		Pre,
		Main,
		Post,
	};
	EStage Stage = EStage::Pre;

	EMappingQueryResult Result = EMappingQueryResult::MappingAvailable;

	// These will be ordered by priority
	for (const UInputMappingContext* BlockingContext : PrioritizedActiveContexts)
	{
		if (!BlockingContext)
		{
			continue;
		}

		// Update stage
		if (Stage == EStage::Main)
		{
			Stage = EStage::Post;
		}
		else if (BlockingContext == InputContext)
		{
			Stage = EStage::Main;
		}

		for (const FEnhancedActionKeyMapping& Mapping : BlockingContext->GetMappings())
		{
			if (Mapping.Key == Key && Mapping.Action)
			{
				FMappingQueryIssue Issue;
				// Block mappings that would have an unintended effect with an existing mapping
				// TODO: This needs to apply chording input consumption rules
				if (Stage == EStage::Pre && Mapping.Action->bConsumeInput)
				{
					Issue.Issue = EMappingQueryIssue::HiddenByExistingMapping;
				}
				else if (Stage == EStage::Post && Action->bConsumeInput)
				{
					Issue.Issue = EMappingQueryIssue::HidesExistingMapping;
				}
				else if (Stage == EStage::Main)
				{
					Issue.Issue = EMappingQueryIssue::CollisionWithMappingInSameContext;
				}

				// Block mapping over any action that refuses it.
				if (Mapping.Action->bReserveAllMappings)
				{
					Issue.Issue = EMappingQueryIssue::ReservedByAction;
				}

				if (Issue.Issue != EMappingQueryIssue::NoIssue)
				{
					Issue.BlockingContext = BlockingContext;
					Issue.BlockingAction = Mapping.Action;
					OutIssues.Add(Issue);

					if ((Issue.Issue & BlockingIssues) != EMappingQueryIssue::NoIssue)
					{
						Result = EMappingQueryResult::NotMappable;
					}
				}
			}
		}
	}

	// Context must be part of the tested collection. If we didn't find it raise an error.
	if (Stage < EStage::Main)
	{
		return EMappingQueryResult::Error_InputContextNotInActiveContexts;
	}

	return Result;

}

bool IEnhancedInputSubsystemInterface::HasTriggerWith(TFunctionRef<bool(const UInputTrigger*)> TestFn, const TArray<UInputTrigger*>& Triggers)
{
	for (const UInputTrigger* Trigger : Triggers)
	{
		if (TestFn(Trigger))
		{
			return true;
		}
	}
	return false;
};

void IEnhancedInputSubsystemInterface::InjectChordBlockers(const TArray<int32>& ChordedMappings)
{
	UEnhancedPlayerInput* PlayerInput = GetPlayerInput();
	if (!PlayerInput)
	{
		return;
	}

	// Inject chord blockers into all lower priority action mappings with a shared key
	for (int32 MappingIndex : ChordedMappings)
	{
		FEnhancedActionKeyMapping& ChordMapping = PlayerInput->EnhancedActionMappings[MappingIndex];
		for (int32 i = MappingIndex + 1; i < PlayerInput->EnhancedActionMappings.Num(); ++i)
		{
			FEnhancedActionKeyMapping& Mapping = PlayerInput->EnhancedActionMappings[i];
			if (Mapping.Action && Mapping.Key == ChordMapping.Key)
			{
				// If we have no explicit triggers we can't inject an implicit as it may cause us to fire when we shouldn't.
				auto AnyExplicit = [](const UInputTrigger* Trigger) { return Trigger->GetTriggerType() == ETriggerType::Explicit; };
				if (!HasTriggerWith(AnyExplicit, Mapping.Triggers) && !HasTriggerWith(AnyExplicit, Mapping.Action->Triggers))
				{
					// Insert a down trigger to ensure we have valid rules for triggering when the chord blocker is active.
					Mapping.Triggers.Add(NewObject<UInputTriggerDown>());
					Mapping.Triggers.Last()->ActuationThreshold = SMALL_NUMBER;	// TODO: "No trigger" actuates on any non-zero value but Down has a threshold so this is a hack to reproduce no trigger behavior!
				}

				UInputTriggerChordBlocker* ChordBlocker = NewObject<UInputTriggerChordBlocker>(PlayerInput);
				ChordBlocker->ChordAction = ChordMapping.Action;
				// TODO: If the chording action is bound at a lower priority than the blocked action its trigger state will be evaluated too late, which may produce unintended effects on the first tick.
				Mapping.Triggers.Add(ChordBlocker);
			}
		}
	}
}

void IEnhancedInputSubsystemInterface::ApplyAxisPropertyModifiers(UEnhancedPlayerInput* PlayerInput, FEnhancedActionKeyMapping& Mapping) const
{
	// Axis properties are treated as per-key default modifier layouts.

	// TODO: Make this optional? Opt in or out? Per modifier type?
	//if (!EnhancedInputSettings.bApplyAxisPropertiesAsModifiers)
	//{
	//	return;
	//}

	if (GGlobalAxisConfigMode_None == GGlobalAxisConfigMode)
	{
		return;
	}

	// TODO: This function is causing issues with gamepads, applying a hidden 0.25 deadzone modifier by default. Apply it to mouse inputs only until a better system is in place.
	if (GGlobalAxisConfigMode_All != GGlobalAxisConfigMode &&
		!Mapping.Key.IsMouseButton())
	{
		return;
	}

	// Apply applicable axis property modifiers from the old input settings automatically.
	// TODO: This needs to live at the EnhancedInputSettings level.
	// TODO: Adopt this approach for all modifiers? Most of these are better done at the action level for most use cases.
	FInputAxisProperties AxisProperties;
	if (PlayerInput->GetAxisProperties(Mapping.Key, AxisProperties))
	{
		TArray<UInputModifier*> Modifiers;

		// If a modifier already exists it should override axis properties.
		auto HasExistingModifier = [&Mapping](UClass* OfType)
		{
			auto TypeMatcher = [&OfType](UInputModifier* Modifier) { return Modifier != nullptr && Modifier->IsA(OfType); };
			return Mapping.Modifiers.ContainsByPredicate(TypeMatcher) || Mapping.Action->Modifiers.ContainsByPredicate(TypeMatcher);
		};

		// Maintain old input system modification order.

		if (AxisProperties.DeadZone > 0.f &&
			!HasExistingModifier(UInputModifierDeadZone::StaticClass()))
		{
			UInputModifierDeadZone* DeadZone = NewObject<UInputModifierDeadZone>();
			DeadZone->LowerThreshold = AxisProperties.DeadZone;
			DeadZone->Type = EDeadZoneType::Axial;
			Modifiers.Add(DeadZone);
		}

		if (AxisProperties.Exponent != 1.f &&
			!HasExistingModifier(UInputModifierResponseCurveExponential::StaticClass()))
		{
			UInputModifierResponseCurveExponential* Exponent = NewObject<UInputModifierResponseCurveExponential>();
			Exponent->CurveExponent = FVector::OneVector * AxisProperties.Exponent;
			Modifiers.Add(Exponent);
		}

		// Sensitivity stacks with user defined.
		// TODO: Unexpected behavior but makes sense for most use cases. E.g. Mouse sensitivity, which is scaled by 0.07 in BaseInput.ini, would be broken by adding a Look action sensitivity.
		if (AxisProperties.Sensitivity != 1.f /* &&
			!HasExistingModifier(UInputModifierScalar::StaticClass())*/)
		{
			UInputModifierScalar* Sensitivity = NewObject<UInputModifierScalar>();
			Sensitivity->Scalar = FVector::OneVector * AxisProperties.Sensitivity;
			Modifiers.Add(Sensitivity);
		}

		if (AxisProperties.bInvert &&
			!HasExistingModifier(UInputModifierNegate::StaticClass()))
		{
			Modifiers.Add(NewObject<UInputModifierNegate>());
		}

		// Add to front of modifier list (these modifiers should be executed before any user defined modifiers)
		Swap(Mapping.Modifiers, Modifiers);
		Mapping.Modifiers.Append(Modifiers);
	}
}


bool IEnhancedInputSubsystemInterface::HasMappingContext(const UInputMappingContext* MappingContext) const
{
	return GetPlayerInput() && GetPlayerInput()->AppliedInputContexts.Contains(MappingContext);
}

TArray<FKey> IEnhancedInputSubsystemInterface::QueryKeysMappedToAction(const UInputAction* Action) const
{
	TArray<FKey> MappedKeys;

	if (const UEnhancedPlayerInput* const PlayerInput = GetPlayerInput())
	{
		for (const FEnhancedActionKeyMapping& Mapping : PlayerInput->EnhancedActionMappings)
		{
			if (Mapping.Action == Action)
			{
				MappedKeys.AddUnique(Mapping.Key);
			}
		}
	}

	return MappedKeys;
}

template<typename T>
void DeepCopyPtrArray(const TArray<T*>& From, TArray<T*>& To)
{
	To.Empty(From.Num());
	for (T* ToDuplicate : From)
	{
		if (ToDuplicate)
		{
			To.Add(DuplicateObject<T>(ToDuplicate, nullptr));
		}
	}
}

// TODO: This should be a delegate (along with InjectChordBlockers), moving chording out of the underlying subsystem and enabling implementation of custom mapping handlers.
TArray<FEnhancedActionKeyMapping> ReorderMappings(const TArray<FEnhancedActionKeyMapping>& UnorderedMappings)
{
	// Reorder mappings such that chording mappings > chorded mappings > everything else. This is used to ensure mappings within a single context are evaluated in the correct order to support chording.

	TSet<const UInputAction*> ChordingActions;

	// Gather all chording actions within a mapping's triggers.
	auto GatherChordingActions = [&ChordingActions](const FEnhancedActionKeyMapping& Mapping) {
		bool bFoundChordTrigger = false;
		auto EvaluateTriggers = [&Mapping, &ChordingActions, &bFoundChordTrigger](const TArray<UInputTrigger*>& Triggers) {
			for (const UInputTrigger* Trigger : Triggers)
			{
				if (const UInputTriggerChordAction* ChordTrigger = Cast<const UInputTriggerChordAction>(Trigger))
				{
					ChordingActions.Add(ChordTrigger->ChordAction);
					bFoundChordTrigger = true;
				}
			}
		};
		EvaluateTriggers(Mapping.Triggers);
		EvaluateTriggers(Mapping.Action->Triggers);
		return bFoundChordTrigger;
	};

	// Split chorded mappings (second priority) from all others whilst building a list of chording actions to use for further prioritization.
	TArray<FEnhancedActionKeyMapping> ChordedMappings;
	TArray<FEnhancedActionKeyMapping> OtherMappings;
	OtherMappings.Reserve(UnorderedMappings.Num());		// Mappings will most likely be Other
	for (const FEnhancedActionKeyMapping& Mapping : UnorderedMappings)
	{
		TArray<FEnhancedActionKeyMapping>& MappingArray = GatherChordingActions(Mapping) ? ChordedMappings : OtherMappings;
		MappingArray.Add(Mapping);
	}

	TArray<FEnhancedActionKeyMapping> OrderedMappings;
	OrderedMappings.Reserve(UnorderedMappings.Num());

	// Move chording mappings to the front as they need to be evaluated before chord and blocker triggers
	// TODO: Further ordering of chording mappings may be required should one of them be chorded against another
	auto ExtractChords = [&OrderedMappings, &ChordingActions](TArray<FEnhancedActionKeyMapping>& Mappings) {
		for (int32 i = 0; i < Mappings.Num();)
		{
			if (ChordingActions.Contains(Mappings[i].Action))
			{
				OrderedMappings.Add(Mappings[i]);
				Mappings.RemoveAtSwap(i);	// TODO: Do we care about reordering underlying mappings?
			}
			else
			{
				++i;
			}
		}
	};
	ExtractChords(ChordedMappings);
	ExtractChords(OtherMappings);

	OrderedMappings.Append(MoveTemp(ChordedMappings));
	OrderedMappings.Append(MoveTemp(OtherMappings));
	checkf(OrderedMappings.Num() == UnorderedMappings.Num(), TEXT("Number of mappings changed during reorder."));

	return OrderedMappings;
}

void IEnhancedInputSubsystemInterface::RebuildControlMappings()
{
	if(!bMappingRebuildPending)
	{
		return;
	}

	UEnhancedPlayerInput* PlayerInput = GetPlayerInput();
	if (!PlayerInput)
	{
		// TODO: Prefer to reset bMappingRebuildPending here?
		return;
	}

	// Clear existing mappings, but retain the mapping array for later processing
	TArray<FEnhancedActionKeyMapping> OldMappings(MoveTemp(PlayerInput->EnhancedActionMappings));
	PlayerInput->ClearAllMappings();

	// Order contexts by priority
	TMap<const UInputMappingContext*, int32> OrderedInputContexts = PlayerInput->AppliedInputContexts;
	OrderedInputContexts.ValueSort([](const int32& A, const int32& B) { return A > B; });

	TSet<FKey> AppliedKeys;

	TArray<int32> ChordedMappings;

	for (const TPair<const UInputMappingContext*, int32>& ContextPair : OrderedInputContexts)
	{
		// Don't apply context specific keys immediately, allowing multiple mappings to the same key within the same context if required.
		TArray<FKey> ContextAppliedKeys;

		const UInputMappingContext* MappingContext = ContextPair.Key;
		TArray<FEnhancedActionKeyMapping>  OrderedMappings = ReorderMappings(MappingContext->GetMappings());

		for (const FEnhancedActionKeyMapping& Mapping : OrderedMappings)
		{
			if (Mapping.Action && !AppliedKeys.Contains(Mapping.Key))
			{
				// TODO: Wasteful query as we've already established chord state within ReorderMappings. Store TOptional bConsumeInput per mapping, allowing override? Query override via delegate?
				auto IsChord = [](const UInputTrigger* Trigger) { return Cast<const UInputTriggerChordAction>(Trigger) != nullptr; };
				bool bHasActionChords = HasTriggerWith(IsChord, Mapping.Action->Triggers);
				bool bHasChords = HasTriggerWith(IsChord, Mapping.Triggers) || bHasActionChords;

				// Chorded actions can't consume input or they would hide the action they are chording.
				if (!bHasChords && Mapping.Action->bConsumeInput)
				{
					ContextAppliedKeys.Add(Mapping.Key);
				}

				int32 NewMappingIndex = PlayerInput->AddMapping(Mapping);
				FEnhancedActionKeyMapping& NewMapping = PlayerInput->EnhancedActionMappings[NewMappingIndex];

				// Re-instance modifiers
				DeepCopyPtrArray<UInputModifier>(Mapping.Modifiers, NewMapping.Modifiers);

				ApplyAxisPropertyModifiers(PlayerInput, NewMapping);

				// Perform a modifier calculation pass on the default data to initialize values correctly.
				PlayerInput->InitializeMappingActionModifiers(NewMapping);

				// Re-instance triggers
				DeepCopyPtrArray<UInputTrigger>(Mapping.Triggers, NewMapping.Triggers);

				if (bHasChords)
				{
					// TODO: Re-prioritize chorded mappings (within same context only?) by number of chorded actions, so Ctrl + Alt + [key] > Ctrl + [key] > [key].
					// TODO: Above example shouldn't block [key] if only Alt is down, as there is no direct Alt + [key] mapping.y
					ChordedMappings.Add(NewMappingIndex);

					// Action level chording triggers need to be evaluated at the mapping level to ensure they block early enough.
					// TODO: Continuing to evaluate these at the action level is redundant.
					if (bHasActionChords)
					{
						for (const UInputTrigger* Trigger : Mapping.Action->Triggers)
						{
							if (IsChord(Trigger))
							{
								NewMapping.Triggers.Add(DuplicateObject(Trigger, nullptr));
							}
						}
					}
				}
			}
		}

		AppliedKeys.Append(MoveTemp(ContextAppliedKeys));
	}

	InjectChordBlockers(ChordedMappings);

	PlayerInput->ForceRebuildingKeyMaps(false);

	// Remove action instance data for actions that are not referenced in the new action mappings
	TSet<const UInputAction*> RemovedActions;
	for (TPair<const UInputAction*, FInputActionInstance>& ActionInstance : PlayerInput->ActionInstanceData)
	{
		RemovedActions.Add(ActionInstance.Key);
	}
	for (FEnhancedActionKeyMapping& Mapping : PlayerInput->EnhancedActionMappings)
	{
		RemovedActions.Remove(Mapping.Action);

		// Retain old mapping trigger/modifier state for identical key -> action mappings.
		TArray<FEnhancedActionKeyMapping>::SizeType Idx = OldMappings.IndexOfByPredicate([&Mapping](const FEnhancedActionKeyMapping& Other) {return Mapping.Action == Other.Action && Mapping.Key == Other.Key; });
		if (Idx != INDEX_NONE)
		{
			Mapping = MoveTemp(OldMappings[Idx]);
			OldMappings.RemoveAtSwap(Idx);
		}
	}
	for (const UInputAction* Action : RemovedActions)
	{
		PlayerInput->ActionInstanceData.Remove(Action);
	}

	bMappingRebuildPending = false;
}

template<typename T>
void InjectKey(T* InjectVia, FKey Key, const FInputActionValue& Value, float DeltaTime)
{
	// TODO: Overwrite PlayerInput->KeyStateMap directly to block device inputs whilst these are active?
	// TODO: Multi axis FKey support
	if (Key.IsAnalog())
	{
		InjectVia->InputAxis(Key, Value.Get<float>(), DeltaTime, 1, Key.IsGamepadKey());
	}
	else
	{
		// TODO: IE_Repeat support. Ideally ticking at whatever rate the application platform is sending repeat key messages.
		InjectVia->InputKey(Key, IE_Pressed, Value.Get<float>(), Key.IsGamepadKey());
	}
}

void IEnhancedInputSubsystemInterface::TickForcedInput(float DeltaTime)
{
	UEnhancedPlayerInput* PlayerInput = GetPlayerInput();
	if (!PlayerInput)
	{
		return;
	}

	// Forced action triggering
	for (TPair<TWeakObjectPtr<const UInputAction>, FInputActionValue>& ForcedActionPair : ForcedActions)
	{
		TWeakObjectPtr<const UInputAction>& Action = ForcedActionPair.Key;
		if (const UInputAction* InputAction = Action.Get())
		{
			PlayerInput->InjectInputForAction(InputAction, ForcedActionPair.Value);	// TODO: Support modifiers and triggers?
		}
	}

	// Forced key presses
	for (const TPair<FKey, FInputActionValue>& ForcedKeyPair : ForcedKeys)
	{
		// Prefer sending the key pressed event via a player controller if one is available.
		if (APlayerController* Controller = Cast<APlayerController>(PlayerInput->GetOuter()))
		{
			InjectKey(Controller, ForcedKeyPair.Key, ForcedKeyPair.Value, DeltaTime);
		}
		else
		{
			InjectKey(PlayerInput, ForcedKeyPair.Key, ForcedKeyPair.Value, DeltaTime);
		}
	}
}

void IEnhancedInputSubsystemInterface::ApplyForcedInput(const UInputAction* Action, FInputActionValue Value)
{
	check(Action);
	ForcedActions.Emplace(Action, Value);		// TODO: Support modifiers and triggers?
}

void IEnhancedInputSubsystemInterface::ApplyForcedInput(FKey Key, FInputActionValue Value)
{
	check(Key.IsValid());
	ForcedKeys.Emplace(Key, Value);
}

void IEnhancedInputSubsystemInterface::RemoveForcedInput(const UInputAction* Action)
{
	ForcedActions.Remove(Action);
}

void IEnhancedInputSubsystemInterface::RemoveForcedInput(FKey Key)
{
	check(Key.IsValid());
	ForcedKeys.Remove(Key);

	if (UEnhancedPlayerInput* PlayerInput = GetPlayerInput())
	{
		// Prefer sending the key released event via a player controller if one is available.
		if (APlayerController* Controller = Cast<APlayerController>(PlayerInput->GetOuter()))
		{
			Controller->InputKey(Key, EInputEvent::IE_Released, 0.f, Key.IsGamepadKey());
		}
		else
		{
			PlayerInput->InputKey(Key, EInputEvent::IE_Released, 0.f, Key.IsGamepadKey());
		}
	}
}