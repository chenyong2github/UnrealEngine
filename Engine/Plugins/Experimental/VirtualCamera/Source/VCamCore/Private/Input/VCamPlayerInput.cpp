// Copyright Epic Games, Inc. All Rights Reserved.

#include "Input/VCamPlayerInput.h"

#include "LogVCamCore.h"

namespace UE::VCamCore::Private
{
	static FString LexInputEvent(EInputEvent InputEvent)
	{
		switch (InputEvent)
		{
		case IE_Pressed: return TEXT("pressed");
		case IE_Released: return TEXT("released");
		case IE_Repeat: return TEXT("repeat");
		case IE_DoubleClick: return TEXT("double-click");
		case IE_Axis: return TEXT("axis");
		case IE_MAX: // pass-through
		default: return TEXT("invalid");
		}
	}
	
	static FString ToString(const FInputKeyParams& Params)
	{
		return FString::Printf(TEXT("{ InputID: %d, Key: %s, EInputEvent: %s, bIsGamepad: %s }"),
			Params.InputDevice.GetId(),
			*Params.Key.ToString(),
			*LexInputEvent(Params.Event),
			Params.IsGamepad() ? TEXT("true") : TEXT("false")
			);
	}

	static void LogInput(FVCamInputDeviceConfig InputDeviceSettings, const FInputKeyParams& Params, bool bIsFilteredOut)
	{
		switch (InputDeviceSettings.LoggingMode)
		{
		case EVCamInputLoggingMode::OnlyConsumable:
			if (!bIsFilteredOut)
			{
				UE_LOG(LogVCamInputDebug, Log, TEXT("%s"), *ToString(Params));
			}
			break;
		case EVCamInputLoggingMode::OnlyGamepad:
			if (Params.IsGamepad())
			{
				UE_LOG(LogVCamInputDebug, Log, TEXT("%s"), *ToString(Params));
			}
			break;
		case EVCamInputLoggingMode::AllExceptMouse:
			if (!Params.Key.IsMouseButton())
			{
				UE_LOG(LogVCamInputDebug, Log, TEXT("%s"), *ToString(Params));
			}
			break;
		
		case EVCamInputLoggingMode::All:
			UE_LOG(LogVCamInputDebug, Log, TEXT("%s"), *ToString(Params));
		default: ;
		}
	}
}

bool UVCamPlayerInput::InputKey(const FInputKeyParams& Params)
{
	const bool bIsKeyboard = !Params.IsGamepad() && !Params.Key.IsAnalog() && !Params.Key.IsMouseButton() && !Params.Key.IsTouch()
		// Keyboard is always mapped to 0
		&& Params.InputDevice.GetId() == 0;
	const bool bCanCheckAllowList = Params.InputDevice != INPUTDEVICEID_NONE && !bIsKeyboard;
	
	const bool bSkipGamepad = Params.IsGamepad() && (InputDeviceSettings.GamepadInputMode != EVCamGamepadInputMode::Allow);
	const bool bSkipMouse = InputDeviceSettings.MouseInputMode == EVCamInputMode::Ignore && Params.Key.IsMouseButton();
	const bool bSkipKeyboard = InputDeviceSettings.KeyboardInputMode == EVCamInputMode::Ignore && bIsKeyboard;
	const bool bSkipNonAllowListed = bCanCheckAllowList && !InputDeviceSettings.AllowedInputDeviceIds.Contains(Params.InputDevice.GetId());
	const bool bIsFilteredOut = bSkipGamepad || bSkipMouse || bSkipKeyboard || bSkipNonAllowListed;
	
	UE::VCamCore::Private::LogInput(InputDeviceSettings, Params, bIsFilteredOut);
	if (!bIsFilteredOut)
	{
		return Super::InputKey(Params);
	}

	const bool bSkippedGamepad = bSkipGamepad && InputDeviceSettings.GamepadInputMode == EVCamGamepadInputMode::IgnoreAndConsume;
	return bSkippedGamepad;
}

void UVCamPlayerInput::ProcessInputStack(const TArray<UInputComponent*>& InputComponentStack, const float DeltaTime, const bool bGamePaused)
{
	Super::ProcessInputStack(InputComponentStack, DeltaTime, bGamePaused);
}

void UVCamPlayerInput::SetInputSettings(const FVCamInputDeviceConfig& Input)
{
	InputDeviceSettings = Input;
}
