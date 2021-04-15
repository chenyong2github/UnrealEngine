// Copyright Epic Games, Inc. All Rights Reserved.

#include "CommonGameViewportClient.h"
#include "Engine/Console.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"

#if WITH_EDITOR
#include "Editor.h"
#include "EditorViewportClient.h"
#endif // WITH_EDITOR

#include "Input/CommonUIActionRouterBase.h"
#include "Framework/Application/SlateUser.h"

#define LOCTEXT_NAMESPACE ""

static const FName NAME_Typing = FName(TEXT("Typing"));
static const FName NAME_Open = FName(TEXT("Open"));

UCommonGameViewportClient::UCommonGameViewportClient(FVTableHelper& Helper) : Super(Helper)
{
	OnRerouteInput().BindUObject(this, &UCommonGameViewportClient::HandleRerouteInput);
	OnRerouteAxis().BindUObject(this, &UCommonGameViewportClient::HandleRerouteAxis);
	OnRerouteTouch().BindUObject(this, &UCommonGameViewportClient::HandleRerouteTouch);
}

UCommonGameViewportClient::~UCommonGameViewportClient()
{
}

bool UCommonGameViewportClient::InputKey(const FInputKeyEventArgs& InEventArgs)
{
	const FInputKeyEventArgs& EventArgs = InEventArgs;

	if (IsKeyPriorityAboveUI(EventArgs))
	{
		return true;
	}

	// The input is fair game for handling - the UI gets first dibs
#if !UE_BUILD_SHIPPING
	if (ViewportConsole && !ViewportConsole->ConsoleState.IsEqual(NAME_Typing) && !ViewportConsole->ConsoleState.IsEqual(NAME_Open))
#endif
	{		
		FReply Result = FReply::Unhandled();
		OnRerouteInput().ExecuteIfBound(EventArgs.ControllerId, EventArgs.Key, EventArgs.Event, Result);
		if (Result.IsEventHandled())
		{
			return true;
		}
	}

	return Super::InputKey(EventArgs);
}

bool UCommonGameViewportClient::InputAxis(FViewport* InViewport, int32 UserId, FKey Key, float Delta, float DeltaTime, int32 NumSamples, bool bGamepad)
{
	int32 ControllerId = UserId;
	FReply RerouteResult = FReply::Unhandled();

	OnRerouteAxis().ExecuteIfBound(ControllerId, Key, Delta, RerouteResult);
	if (RerouteResult.IsEventHandled())
	{
		return true;
	}
	return Super::InputAxis(InViewport, ControllerId, Key, Delta, DeltaTime, NumSamples, bGamepad);
}

bool UCommonGameViewportClient::InputTouch(FViewport* InViewport, int32 ControllerId, uint32 Handle, ETouchType::Type Type, const FVector2D& TouchLocation, float Force, FDateTime DeviceTimestamp, uint32 TouchpadIndex)
{
#if !UE_BUILD_SHIPPING
	if (ViewportConsole != NULL && (ViewportConsole->ConsoleState != NAME_Typing) && (ViewportConsole->ConsoleState != NAME_Open))
#endif
	{
		if (OnRerouteTouch().IsBound())
		{
			FReply Result = FReply::Unhandled();
			OnRerouteTouch().Execute(ControllerId, Handle, Type, TouchLocation, Result);
			if (Result.IsEventHandled())
			{
				return true;
			}
		}
	}

	return Super::InputTouch(InViewport, ControllerId, Handle, Type, TouchLocation, Force, DeviceTimestamp, TouchpadIndex);
}

void UCommonGameViewportClient::HandleRerouteInput(int32 ControllerId, FKey Key, EInputEvent EventType, FReply& Reply)
{
	ULocalPlayer* LocalPlayer = GameInstance->FindLocalPlayerFromControllerId(ControllerId);
	Reply = FReply::Unhandled();

	UCommonUIActionRouterBase* ActionRouter = LocalPlayer->GetSubsystem<UCommonUIActionRouterBase>();
	if (ensure(ActionRouter))
	{
		ERouteUIInputResult InputResult = ActionRouter->ProcessInput(Key, EventType);
		if (InputResult == ERouteUIInputResult::BlockGameInput)
		{
			OnRerouteBlockedInput().ExecuteIfBound(ControllerId, Key, EventType, Reply);
		}
		else if (InputResult == ERouteUIInputResult::Handled)
		{
			Reply = FReply::Handled();
		}
	}
}

void UCommonGameViewportClient::HandleRerouteAxis(int32 ControllerId, FKey Key, float Delta, FReply& Reply)
{
	ULocalPlayer* LocalPlayer = GameInstance->FindLocalPlayerFromControllerId(ControllerId);
	Reply = FReply::Unhandled();

	UCommonUIActionRouterBase* ActionRouter = LocalPlayer->GetSubsystem<UCommonUIActionRouterBase>();
	if (ensure(ActionRouter))
	{
		// We don't actually use axis inputs that reach the game viewport UI land for anything, we just want block them reaching the game when they shouldn't
		if (!ActionRouter->CanProcessNormalGameInput())
		{
			Reply = FReply::Handled();
		}
	}
}

void UCommonGameViewportClient::HandleRerouteTouch(int32 ControllerId, uint32 TouchId, ETouchType::Type TouchType, const FVector2D& TouchLocation, FReply& Reply)
{
	ULocalPlayer* LocalPlayer = GameInstance->FindLocalPlayerFromControllerId(ControllerId);
	Reply = FReply::Unhandled();

	if (TouchId < EKeys::NUM_TOUCH_KEYS)
	{
		FKey KeyPressed = EKeys::TouchKeys[TouchId];
		if (KeyPressed.IsValid())
		{
			UCommonUIActionRouterBase* ActionRouter = LocalPlayer->GetSubsystem<UCommonUIActionRouterBase>();
			if (ensure(ActionRouter))
			{
				//@todo DanH: Does anyone actually use this? Do we need to support holds or something with this?
				EInputEvent SimilarInputEvent = IE_MAX;
				switch (TouchType)
				{
				case ETouchType::Began:
					SimilarInputEvent = IE_Pressed;
					break;
				case ETouchType::Ended:
					SimilarInputEvent = IE_Released;
					break;
				default:
					SimilarInputEvent = IE_Repeat;
					break;
				}

				if (ActionRouter->ProcessInput(KeyPressed, SimilarInputEvent) != ERouteUIInputResult::Unhandled)
				{
					Reply = FReply::Handled();
				}
			}
		}
	}
}

bool UCommonGameViewportClient::IsKeyPriorityAboveUI(const FInputKeyEventArgs& EventArgs)
{
#if !UE_BUILD_SHIPPING
	// First priority goes to the viewport console regardless any state or setting
	if (ViewportConsole && ViewportConsole->InputKey(EventArgs.ControllerId, EventArgs.Key, EventArgs.Event, EventArgs.AmountDepressed, EventArgs.IsGamepad()))
	{
		return true;
	}
#endif

	// We'll also treat toggling fullscreen as a system-level sort of input that isn't affected by input filtering
	if (TryToggleFullscreenOnInputKey(EventArgs.Key, EventArgs.Event))
	{
		return true;
	}

	return false;
}

#undef LOCTEXT_NAMESPACE
