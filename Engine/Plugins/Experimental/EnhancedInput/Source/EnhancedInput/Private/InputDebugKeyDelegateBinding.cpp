// Copyright Epic Games, Inc. All Rights Reserved.

#include "InputDebugKeyDelegateBinding.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/Actor.h"

void UInputDebugKeyDelegateBinding::BindToInputComponent(UInputComponent* InputComponent) const
{
#if DEV_ONLY_KEY_BINDINGS_AVAILABLE
	UEnhancedInputComponent* Component = Cast<UEnhancedInputComponent>(InputComponent);
	if (!Component)
	{
		return;
	}

	for(const FBlueprintInputDebugKeyDelegateBinding& Binding : InputDebugKeyDelegateBindings)
	{
		UObject* Owner = CastChecked<UObject>(Component->GetOwner());
		Component->BindDebugKey(Binding.InputChord, Binding.InputKeyEvent, Owner, Binding.FunctionNameToBind, Binding.bExecuteWhenPaused);
	}
#endif
}
