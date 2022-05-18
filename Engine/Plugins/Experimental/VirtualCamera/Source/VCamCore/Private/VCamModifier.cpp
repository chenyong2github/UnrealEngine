// Copyright Epic Games, Inc. All Rights Reserved.

#include "VCamModifier.h"
#include "VCamComponent.h"
#include "VCamTypes.h"
#include "EnhancedInputComponent.h"
#include "Engine/InputDelegateBinding.h"


void UVCamBlueprintModifier::Initialize(UVCamModifierContext* Context, UInputComponent* InputComponent)
{
	// Forward the Initialize call to the Blueprint Event
	{
		FEditorScriptExecutionGuard ScriptGuard;

		OnInitialize(Context);
	}

	UVCamModifier::Initialize(Context, InputComponent);
}

void UVCamBlueprintModifier::Apply(UVCamModifierContext* Context, UCineCameraComponent* CameraComponent, const float DeltaTime)
{
	// Forward the Apply call to the Blueprint Event
	{
		FEditorScriptExecutionGuard ScriptGuard;

		OnApply(Context, CameraComponent, DeltaTime);
	}
}

const UInputMappingContext* UVCamBlueprintModifier::GetInputMappingContext(int32& InputPriority) const
{
	// Forward the call to the Blueprint implementation
	{
		FEditorScriptExecutionGuard ScriptGuard;

		UInputMappingContext* MappingContext = nullptr;
		GetInputMappingContextAndPriority(MappingContext, InputPriority);
		return MappingContext;
	}
}

void UVCamModifier::Initialize(UVCamModifierContext* Context, UInputComponent* InputComponent /* = nullptr */)
{
	// Binds any dynamic input delegates to the provided input component
	if (IsValid(InputComponent))
	{
		UInputDelegateBinding::BindInputDelegates(GetClass(), InputComponent, this);
	}

	bRequiresInitialization = false;
}

void UVCamModifier::PostLoad()
{
	Super::PostLoad();

	bRequiresInitialization = true;
}

UVCamComponent* UVCamModifier::GetOwningVCamComponent() const
{
	return GetTypedOuter<UVCamComponent>();
}

void UVCamModifier::GetCurrentLiveLinkDataFromOwningComponent(FLiveLinkCameraBlueprintData& LiveLinkData)
{
	if (UVCamComponent* OwningComponent = GetOwningVCamComponent())
	{
		OwningComponent->GetLiveLinkDataForCurrentFrame(LiveLinkData);
	}
}

void UVCamModifier::SetEnabled(bool bNewEnabled)
{
	if (FModifierStackEntry* StackEntry = GetCorrespondingStackEntry())
	{
		StackEntry->bEnabled = bNewEnabled;
	}
}

bool UVCamModifier::IsEnabled() const
{
	if (FModifierStackEntry* StackEntry = GetCorrespondingStackEntry())
	{
		return StackEntry->bEnabled;
	}

	return false;
}

const UInputMappingContext* UVCamModifier::GetInputMappingContext(int32& InputPriority) const
{
	return nullptr;
}

FModifierStackEntry* UVCamModifier::GetCorrespondingStackEntry() const
{
	FModifierStackEntry* StackEntry = nullptr;

	if (UVCamComponent* ParentComponent = GetOwningVCamComponent())
	{
		StackEntry = ParentComponent->ModifierStack.FindByPredicate([this](const FModifierStackEntry& StackEntryToTest) 
		{
			return StackEntryToTest.GeneratedModifier == this;
		});
	}

	return StackEntry;
}
