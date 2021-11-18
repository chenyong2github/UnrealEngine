// Copyright Epic Games, Inc. All Rights Reserved.

#include "InputAction.h"

#include "EnhancedPlayerInput.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
#include "UObject/UObjectIterator.h"

FInputActionInstance::FInputActionInstance(const UInputAction* InSourceAction)
	: SourceAction(InSourceAction)
{
	if (ensureMsgf(SourceAction != nullptr, TEXT("Trying to create an FInputActionInstance without a source action")))
	{
		Value = FInputActionValue(SourceAction->ValueType, FVector::ZeroVector);

		Triggers.Reserve(SourceAction->Triggers.Num());
		for (UInputTrigger* ToDuplicate : SourceAction->Triggers)
		{
			if (ToDuplicate)
			{
				Triggers.Add(DuplicateObject<UInputTrigger>(ToDuplicate, nullptr));
			}
		}

		for (UInputModifier* ToDuplicate : SourceAction->Modifiers)
		{
			if (ToDuplicate)
			{
				Modifiers.Add(DuplicateObject<UInputModifier>(ToDuplicate, nullptr));
			}
		}

	}
}

#if WITH_EDITOR
// Record input action property changes for later processing

TSet<const UInputAction*> UInputAction::ActionsWithModifiedValueTypes;

void UInputAction::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// If our value type changes we need to inform any blueprint InputActionEx nodes that refer to this action
	if (PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UInputAction, ValueType) ||
		PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UInputAction, Triggers))
	{
		ActionsWithModifiedValueTypes.Add(this);
	}
}

ETriggerEventsSupported UInputAction::GetSupportedTriggerEvents() const
{
	ETriggerEventsSupported SupportedTriggers = ETriggerEventsSupported::None;

	bool bTriggersAdded = false;
	
	if(!Triggers.IsEmpty())
	{
		for(const UInputTrigger* Trigger : Triggers)
		{
			if(Trigger)
			{
				EnumAddFlags(SupportedTriggers, Trigger->GetSupportedTriggerEvents());
				bTriggersAdded = true;
			}
		}
	}

	// If there are no triggers on an action, then it can be instant (a key is pressed/release) or happening over time(key is held down)
	if(!bTriggersAdded)
	{
		EnumAddFlags(SupportedTriggers, ETriggerEventsSupported::Instant | ETriggerEventsSupported::Uninterruptible);
	}

	return SupportedTriggers;
}

#endif