// Copyright Epic Games, Inc. All Rights Reserved.

#include "CommonVisibilityWidgetBase.h"
#include "CommonUIPrivatePCH.h"
#include "CommonUISubsystemBase.h"

#include "Components/BorderSlot.h"
#include "Widgets/Layout/SBox.h"
#include "Styling/CoreStyle.h"
#include "CommonInputBaseTypes.h"

UCommonVisibilityWidgetBase::UCommonVisibilityWidgetBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bShowForGamepad(true)
	, bShowForMouseAndKeyboard(true)
	, bShowForTouch(true)
	, VisibleType(ESlateVisibility::SelfHitTestInvisible)
	, HiddenType(ESlateVisibility::Collapsed)
{
	//@TODO: The duplication of FNames is a bit of a memory waste.
	for (const FName& RegisteredPlatform : FCommonInputPlatformBaseData::GetRegisteredPlatforms())
	{
		VisibilityControls.Add(RegisteredPlatform, false);
	}
}

void UCommonVisibilityWidgetBase::OnWidgetRebuilt()
{
	Super::OnWidgetRebuilt();
	UpdateVisibility();
	ListenToInputMethodChanged();
}

void UCommonVisibilityWidgetBase::UpdateVisibility()
{
	if (!IsDesignTime())
	{
		const UCommonInputSubsystem* CommonInputSubsystem = UCommonInputSubsystem::Get(GetOwningLocalPlayer());
		if (ensure(CommonInputSubsystem))
		{
			bool bVisibleForInput = bShowForMouseAndKeyboard;

			if (CommonInputSubsystem->IsInputMethodActive(ECommonInputType::Gamepad))
			{
				bVisibleForInput = bShowForGamepad;
			}
			else if (CommonInputSubsystem->IsInputMethodActive(ECommonInputType::Touch))
			{
				bVisibleForInput = bShowForTouch;
			}

			bool bVisibleForPlatform = VisibilityControls[FCommonInputBase::GetCurrentPlatformName()];
			SetVisibility(bVisibleForPlatform && bVisibleForInput ? VisibleType : HiddenType);
		}
	}
}

void UCommonVisibilityWidgetBase::ListenToInputMethodChanged(bool bListen)
{
	if (IsDesignTime())
	{
		return;
	}

	if (UCommonInputSubsystem* CommonInputSubsystem = UCommonInputSubsystem::Get(GetOwningLocalPlayer()))
	{
		CommonInputSubsystem->OnInputMethodChangedNative.RemoveAll(this);
		if (bListen)
		{
			CommonInputSubsystem->OnInputMethodChangedNative.AddUObject(this, &ThisClass::HandleInputMethodChanged);
		}
	}
}

void UCommonVisibilityWidgetBase::HandleInputMethodChanged(ECommonInputType input)
{
	UpdateVisibility();
}

const TArray<FName>& UCommonVisibilityWidgetBase::GetRegisteredPlatforms()
{
	return FCommonInputPlatformBaseData::GetRegisteredPlatforms();
}
