// Copyright Epic Games, Inc. All Rights Reserved.

#include "VCamModifier.h"
#include "VCamComponent.h"
#include "VCamTypes.h"


void UVCamBlueprintModifier::Initialize(UVCamModifierContext* Context)
{
	// Forward the Initialize call to the Blueprint Event
	{
		FEditorScriptExecutionGuard ScriptGuard;

		OnInitialize(Context);
	}

	UVCamModifier::Initialize(Context);
}

void UVCamBlueprintModifier::Apply(UVCamModifierContext* Context, UCineCameraComponent* CameraComponent, const float DeltaTime)
{
	// Forward the Apply call to the Blueprint Event
	{
		FEditorScriptExecutionGuard ScriptGuard;

		OnApply(Context, CameraComponent, DeltaTime);
	}
}

void UVCamModifier::Initialize(UVCamModifierContext* Context)
{
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