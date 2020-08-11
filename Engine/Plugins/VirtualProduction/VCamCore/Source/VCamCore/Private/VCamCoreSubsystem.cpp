// Copyright Epic Games, Inc. All Rights Reserved.

#include "VCamCoreSubsystem.h"

#include "Framework/Application/SlateApplication.h"

DEFINE_LOG_CATEGORY(LogVCamCore);

UVCamCoreSubsystem::UVCamCoreSubsystem()
{
	// Registering the input processor is only valid in the actual subsystem and not the CDO
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		InputProcessor = MakeShared<FEditorInputProcessor>();
		if (FSlateApplication::IsInitialized())
		{
			bIsRegisterd = FSlateApplication::Get().RegisterInputPreProcessor(InputProcessor);
		}
	}
}

UVCamCoreSubsystem::~UVCamCoreSubsystem()
{
	if (FSlateApplication::IsInitialized() && InputProcessor.IsValid())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(InputProcessor);
	}
}


void UVCamCoreSubsystem::SetShouldConsumeGamepadInput(const bool bInShouldConsumeGamepadInput)
{
	if (InputProcessor.IsValid())
	{
		InputProcessor->bShouldConsumeGamepadInput = bInShouldConsumeGamepadInput;
	}
}

bool UVCamCoreSubsystem::GetShouldConsumeGamepadInput() const
{
	bool bShouldConsumeGamepadInput = false;
	if (InputProcessor.IsValid())
	{
		bShouldConsumeGamepadInput = InputProcessor->bShouldConsumeGamepadInput;
	}
	return bShouldConsumeGamepadInput;
}

void UVCamCoreSubsystem::BindKeyDownEvent(const FKey Key, FKeyInputDelegate Delegate)
{
	if (InputProcessor.IsValid())
	{
		InputProcessor->KeyDownDelegateStore.AddDelegate(Key, Delegate);
	}
}

void UVCamCoreSubsystem::BindKeyUpEvent(const FKey Key, FKeyInputDelegate Delegate)
{
	if (InputProcessor.IsValid())
	{
		InputProcessor->KeyUpDelegateStore.AddDelegate(Key, Delegate);
	}
}

void UVCamCoreSubsystem::BindAnalogEvent(const FKey Key, FAnalogInputDelegate Delegate)
{
	if (InputProcessor.IsValid())
	{
		InputProcessor->AnalogDelegateStore.AddDelegate(Key, Delegate);
	}
}

void UVCamCoreSubsystem::BindMouseMoveEvent(FPointerInputDelegate Delegate)
{
	if (InputProcessor.IsValid())
	{
		InputProcessor->MouseMoveDelegateStore.AddDelegate(EKeys::Invalid, Delegate);
	}
}

void UVCamCoreSubsystem::BindMouseButtonDownEvent(const FKey Key, FPointerInputDelegate Delegate)
{
	if (InputProcessor.IsValid())
	{
		InputProcessor->MouseButtonDownDelegateStore.AddDelegate(Key, Delegate);
	}
}

void UVCamCoreSubsystem::BindMouseButtonUpEvent(const FKey Key, FPointerInputDelegate Delegate)
{
	if (InputProcessor.IsValid())
	{
		InputProcessor->MouseButtonUpDelegateStore.AddDelegate(Key, Delegate);
	}
}

void UVCamCoreSubsystem::BindMouseDoubleClickEvent(const FKey Key, FPointerInputDelegate Delegate)
{
	if (InputProcessor.IsValid())
	{
		InputProcessor->MouseButtonDoubleClickDelegateStore.AddDelegate(Key, Delegate);
	}
}

void UVCamCoreSubsystem::BindMouseWheelEvent(FPointerInputDelegate Delegate)
{
	if (InputProcessor.IsValid())
	{
		InputProcessor->MouseWheelDelegateStore.AddDelegate(EKeys::Invalid, Delegate);
	}
}
