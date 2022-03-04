// Copyright Epic Games, Inc. All Rights Reserved.

#include "InputDebugKeyDelegateBinding.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/Actor.h"

void UInputDebugKeyDelegateBinding::BindToInputComponent(UInputComponent* InputComponent, UObject* ObjectToBindTo) const
{
#if DEV_ONLY_KEY_BINDINGS_AVAILABLE
	UEnhancedInputComponent* Component = Cast<UEnhancedInputComponent>(InputComponent);
	if (!Component)
	{
		return;
	}

	for(const FBlueprintInputDebugKeyDelegateBinding& Binding : InputDebugKeyDelegateBindings)
	{
		Component->BindDebugKey(Binding.InputChord, Binding.InputKeyEvent, ObjectToBindTo, Binding.FunctionNameToBind, Binding.bExecuteWhenPaused);
	}
#endif
}
