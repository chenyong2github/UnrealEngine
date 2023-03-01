// Copyright Epic Games, Inc. All Rights Reserved.

#include "InputDeviceDetectionProcessor.h"

namespace UE::VCamCoreEditor::Private
{
	TSharedPtr<FInputDeviceDetectionProcessor> FInputDeviceDetectionProcessor::MakeAndRegister(FOnInputDeviceDetected OnInputDeviceDetectedDelegate)
	{
		if (ensure(FSlateApplication::IsInitialized()))
		{
			TSharedRef<FInputDeviceDetectionProcessor> Result = MakeShared<FInputDeviceDetectionProcessor>(MoveTemp(OnInputDeviceDetectedDelegate));
			FSlateApplication::Get().RegisterInputPreProcessor(Result, 0);
			return Result;
		}
		return nullptr;
	}

	void FInputDeviceDetectionProcessor::Unregister()
	{
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().UnregisterInputPreProcessor(SharedThis(this));
		}
	}

	FInputDeviceDetectionProcessor::FInputDeviceDetectionProcessor(FOnInputDeviceDetected OnInputDeviceDetectedDelegate)
		: OnInputDeviceDetectedDelegate(MoveTemp(OnInputDeviceDetectedDelegate))
	{}

	bool FInputDeviceDetectionProcessor::HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
	{
		OnInputDeviceDetectedDelegate.Execute(InKeyEvent.GetInputDeviceId().GetId());
		return true;
	}

	bool FInputDeviceDetectionProcessor::HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
	{
		OnInputDeviceDetectedDelegate.Execute(InKeyEvent.GetInputDeviceId().GetId());
		return true;
	}

	bool FInputDeviceDetectionProcessor::HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent)
	{
		OnInputDeviceDetectedDelegate.Execute(InAnalogInputEvent.GetInputDeviceId().GetId());
		return true;
	}
}
