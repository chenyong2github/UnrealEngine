// Copyright Epic Games, Inc. All Rights Reserved.

#include "EnhancedInputActionDelegateBinding.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/Actor.h"

UEnhancedInputActionDelegateBinding::UEnhancedInputActionDelegateBinding(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UEnhancedInputActionDelegateBinding::BindToInputComponent(UInputComponent* InputComponent) const
{
	UEnhancedInputComponent* Component = Cast<UEnhancedInputComponent>(InputComponent);
	if (!Component)
	{
		return;
	}

	for(const FBlueprintEnhancedInputActionBinding& Binding : InputActionDelegateBindings)
	{
		UObject* Owner = CastChecked<UObject>(Component->GetOwner());
		Component->BindAction(Binding.InputAction, Binding.TriggerEvent, Owner, Binding.FunctionNameToBind);
	}
}

UEnhancedInputActionValueBinding::UEnhancedInputActionValueBinding(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UEnhancedInputActionValueBinding::BindToInputComponent(UInputComponent* InputComponent) const
{
	UEnhancedInputComponent* Component = Cast<UEnhancedInputComponent>(InputComponent);
	if (!Component)
	{
		return;
	}

	for (const FBlueprintEnhancedInputActionBinding& Binding : InputActionValueBindings)
	{
		Component->BindActionValue(Binding.InputAction);
	}
}