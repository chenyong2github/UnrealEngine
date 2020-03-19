// Copyright Epic Games, Inc. All Rights Reserved.

#include "Engine/InputDelegateBinding.h"
#include "UObject/Class.h"
#include "Engine/BlueprintGeneratedClass.h"

TSet<UClass*> UInputDelegateBinding::InputBindingClasses;

UInputDelegateBinding::UInputDelegateBinding(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	if (IsTemplate())
	{
		// Auto register the class
		InputBindingClasses.Emplace(GetClass());
	}
}

bool UInputDelegateBinding::SupportsInputDelegate(const UClass* InClass)
{
	return Cast<UDynamicClass>(InClass) || Cast<UBlueprintGeneratedClass>(InClass);
}

void UInputDelegateBinding::BindInputDelegates(const UClass* InClass, UInputComponent* InputComponent)
{
	if (SupportsInputDelegate(InClass))
	{
		BindInputDelegates(InClass->GetSuperClass(), InputComponent);

		for(UClass* BindingClass : InputBindingClasses)
		{
			UInputDelegateBinding* BindingObject = CastChecked<UInputDelegateBinding>(
				UBlueprintGeneratedClass::GetDynamicBindingObject(InClass, BindingClass)
				, ECastCheckedType::NullAllowed);
			if (BindingObject)
			{
				BindingObject->BindToInputComponent(InputComponent);
			}
		}
	}
}