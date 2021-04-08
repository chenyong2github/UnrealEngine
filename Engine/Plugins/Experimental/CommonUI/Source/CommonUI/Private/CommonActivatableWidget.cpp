// Copyright Epic Games, Inc. All Rights Reserved.

#include "CommonActivatableWidget.h"
#include "CommonUIPrivatePCH.h"
#include "Input/CommonUIInputTypes.h"
#include "ICommonInputModule.h"

UCommonActivatableWidget::FActivatableWidgetRebuildEvent UCommonActivatableWidget::OnRebuilding;

void UCommonActivatableWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (bIsBackHandler)
	{
		FBindUIActionArgs BindArgs(ICommonInputModule::GetSettings().GetDefaultBackAction(), FSimpleDelegate::CreateUObject(this, &UCommonActivatableWidget::HandleBackAction));
		BindArgs.bDisplayInActionBar = false;
		RegisterUIActionBinding(BindArgs);
	}

	if (bAutoActivate)
	{
		UE_LOG(LogCommonUI, Verbose, TEXT("[%s] auto-activated"), *GetName());
		ActivateWidget();
	}
}

void UCommonActivatableWidget::NativeDestruct()
{
	if (UGameInstance* GameInstance = GetGameInstance<UGameInstance>())
	{
		// Deactivations might rely on members of the game instance to validly run.
		// If there's no game instance, any cleanup done in Deactivation will be irrelevant; we're shutting down the game
		DeactivateWidget();
	}
	Super::NativeDestruct();
}

UWidget* UCommonActivatableWidget::GetDesiredFocusTarget() const
{
	return NativeGetDesiredFocusTarget();
}

TOptional<FUICameraConfig> UCommonActivatableWidget::GetDesiredCameraConfig() const
{
	return TOptional<FUICameraConfig>();
}

UWidget* UCommonActivatableWidget::NativeGetDesiredFocusTarget() const
{
	return BP_GetDesiredFocusTarget();
}

TOptional<FUIInputConfig> UCommonActivatableWidget::GetDesiredInputConfig() const
{
	// No particular config is desired by default
	return TOptional<FUIInputConfig>();
}

void UCommonActivatableWidget::RequestRefreshFocus()
{
	OnRequestRefreshFocusEvent.Broadcast();
}

void UCommonActivatableWidget::ActivateWidget()
{
	if (!bIsActive)
	{
		InternalProcessActivation();
	}
}
void UCommonActivatableWidget::InternalProcessActivation()
{
	bIsActive = true;
	NativeOnActivated();
}

void UCommonActivatableWidget::DeactivateWidget()
{
	if (bIsActive)
	{
		InternalProcessDeactivation();
	}
}

void UCommonActivatableWidget::InternalProcessDeactivation()
{
	bIsActive = false;
	NativeOnDeactivated();
}

TSharedRef<SWidget> UCommonActivatableWidget::RebuildWidget()
{
	// Note: the scoped builder guards against design-time so we don't need to here (as it'd make the scoped lifetime more awkward to leverage)
	//FScopedActivatableTreeBuilder ScopedBuilder(*this);
	if (!IsDesignTime())
	{
		OnRebuilding.Broadcast(*this);
	}
	
	return Super::RebuildWidget();
}

void UCommonActivatableWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	OnSlateReleased().Broadcast();
}

void UCommonActivatableWidget::NativeOnActivated()
{
	if (ensureMsgf(bIsActive, TEXT("[%s] has called NativeOnActivated, but isn't actually activated! Never call this directly - call ActivateWidget()")))
	{
		if (bSetVisibilityOnActivated)
		{
			SetVisibility(ActivatedVisibility);
			UE_LOG(LogCommonUI, Verbose, TEXT("[%s] set visibility to [%s] on activation"), *GetName(), *StaticEnum<ESlateVisibility>()->GetDisplayValueAsText(ActivatedVisibility).ToString());
		}

		BP_OnActivated();
		OnActivated().Broadcast();
		BP_OnWidgetActivated.Broadcast();
	}
}

void UCommonActivatableWidget::NativeOnDeactivated()
{
	if (ensure(!bIsActive))
	{
		if (bSetVisibilityOnDeactivated)
		{
			SetVisibility(DeactivatedVisibility);
			UE_LOG(LogCommonUI, Verbose, TEXT("[%s] set visibility to [%d] on deactivation"), *GetName(), *StaticEnum<ESlateVisibility>()->GetDisplayValueAsText(DeactivatedVisibility).ToString());
		}

		BP_OnDeactivated();
		OnDeactivated().Broadcast();
		BP_OnWidgetDeactivated.Broadcast();
	}
}

bool UCommonActivatableWidget::NativeOnHandleBackAction()
{
	//@todo DanH: This isn't actually fleshed out enough - we need to figure out whether we want to let back be a normal action or a special one routed through all activatables that lets them conditionally handle it
	if (bIsBackHandler)
	{
		if (!BP_OnHandleBackAction())
		{
			// Default behavior is unconditional deactivation
			UE_LOG(LogCommonUI, Verbose, TEXT("[%s] handled back with default implementation. Deactivating immediately."), *GetName());
			DeactivateWidget();
		}
		return true;
	}
	return false;
}

void UCommonActivatableWidget::HandleBackAction()
{
	NativeOnHandleBackAction();
}

void UCommonActivatableWidget::Reset()
{
	bIsActive = false;

	BP_OnWidgetActivated.Clear();
	BP_OnWidgetDeactivated.Clear();
}
