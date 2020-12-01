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
	if (PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UInputAction, ValueType))
	{
		ActionsWithModifiedValueTypes.Add(this);
	}
}
#endif