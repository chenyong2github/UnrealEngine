// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/FBridgeMessageHandler.h"
#include "UI/BrowserBinding.h"
#include "UI/BridgeUIManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/Geometry.h"
#include "Widgets/SWindow.h"


FBridgeMessageHandler::FBridgeMessageHandler(const TSharedPtr<FGenericApplicationMessageHandler>& InTargetHandler)
	: TargetHandler(InTargetHandler)
{

}

FBridgeMessageHandler::FBridgeMessageHandler()
{
}

FBridgeMessageHandler::~FBridgeMessageHandler()
{
}

void FBridgeMessageHandler::SetTargetHandler(const TSharedPtr<FGenericApplicationMessageHandler>& InTargetHandler)
{
	TargetHandler = InTargetHandler;
}

const TSharedPtr<FGenericApplicationMessageHandler> FBridgeMessageHandler::GetTargetHandler() const
{
	return TargetHandler;
}


bool FBridgeMessageHandler::ShouldProcessUserInputMessages(const TSharedPtr< FGenericWindow >& PlatformWindow) const
{
	if (TargetHandler.IsValid())
	{
		return TargetHandler->ShouldProcessUserInputMessages(PlatformWindow);
	}

	return false;
}

bool FBridgeMessageHandler::OnKeyChar(const TCHAR Character, const bool IsRepeat)
{
	if (TargetHandler.IsValid())
	{
		return TargetHandler->OnKeyChar(Character, IsRepeat);
	}

	return false;
}

bool FBridgeMessageHandler::OnKeyDown(const int32 KeyCode, const uint32 CharacterCode, const bool IsRepeat)
{
	if (TargetHandler.IsValid())
	{
		return TargetHandler->OnKeyDown(KeyCode, CharacterCode, IsRepeat);
	}

	return false;
}

bool FBridgeMessageHandler::OnKeyUp(const int32 KeyCode, const uint32 CharacterCode, const bool IsRepeat)
{
	if (TargetHandler.IsValid())
	{
		return TargetHandler->OnKeyUp(KeyCode, CharacterCode, IsRepeat);
	}

	return false;
}

bool FBridgeMessageHandler::OnMouseDown(const TSharedPtr< FGenericWindow >& Window, const EMouseButtons::Type Button)
{
	if (TargetHandler.IsValid())
	{
		return TargetHandler->OnMouseDown(Window, Button);
	}

	return false;
}

bool FBridgeMessageHandler::OnMouseDown(const TSharedPtr< FGenericWindow >& Window, const EMouseButtons::Type Button, const FVector2D CursorPos)
{
	if (TargetHandler.IsValid())
	{
		return TargetHandler->OnMouseDown(Window, Button, CursorPos);
	}

	return false;
}

// Called on Mac & Linux
bool FBridgeMessageHandler::OnMouseUp(const EMouseButtons::Type Button)
{
	if (TargetHandler.IsValid())
	{
		FBridgeMessageHandler::OnMouseUp();
		return TargetHandler->OnMouseUp(Button);
	}

	return false;
}

// Called on Windows
bool FBridgeMessageHandler::OnMouseUp(const EMouseButtons::Type Button, const FVector2D CursorPos)
{
	if (TargetHandler.IsValid())
	{
		FBridgeMessageHandler::OnMouseUp();
		return TargetHandler->OnMouseUp(Button, CursorPos);
	}

	return false;
}

void FBridgeMessageHandler::OnMouseUp()
{
	// Destroy the drag popups
	FBridgeUIManager::Instance->DragDropWindow->RequestDestroyWindow();

	// Get browser dimensions
	FGeometry BrowserGeometry = FBridgeUIManager::Instance->WebBrowserWidget.Get()->GetTickSpaceGeometry();

	if (!BrowserGeometry.IsUnderLocation(FSlateApplication::Get().GetCursorPos()))
	{
		// Dropped outside browser
		// TODO: Remove these identifiers
		FBridgeUIManager::BrowserBinding->OnDroppedDelegate.Execute(TEXT("dropped-outside"));
	}
	else
	{
		// TODO: Remove these identifiers
		FBridgeUIManager::BrowserBinding->OnDropDiscardedDelegate.Execute(TEXT("dropped-inside"));
	}

	FSlateApplication::Get().GetPlatformApplication()->SetMessageHandler(TargetHandler.ToSharedRef());
}

bool FBridgeMessageHandler::OnMouseDoubleClick(const TSharedPtr< FGenericWindow >& Window, const EMouseButtons::Type Button)
{
	if (TargetHandler.IsValid())
	{
		return TargetHandler->OnMouseDoubleClick(Window, Button);
	}

	return false;
}

bool FBridgeMessageHandler::OnMouseDoubleClick(const TSharedPtr< FGenericWindow >& Window, const EMouseButtons::Type Button, const FVector2D CursorPos)
{
	if (TargetHandler.IsValid())
	{
		return TargetHandler->OnMouseDoubleClick(Window, Button, CursorPos);
	}

	return false;
}

bool FBridgeMessageHandler::OnMouseWheel(const float Delta)
{
	if (TargetHandler.IsValid())
	{
		return TargetHandler->OnMouseWheel(Delta);
	}

	return false;
}

bool FBridgeMessageHandler::OnMouseWheel(const float Delta, const FVector2D CursorPos)
{
	if (TargetHandler.IsValid())
	{
		return TargetHandler->OnMouseWheel(Delta, CursorPos);
	}

	return false;
}

bool FBridgeMessageHandler::OnMouseMove()
{
	TSharedPtr<SWindow> DragDropWindow = FBridgeUIManager::Instance->DragDropWindow;
	FVector2D DragDropWindowSize = DragDropWindow->GetTickSpaceGeometry().GetAbsoluteSize();
	FVector2D CursorPosition = FSlateApplication::Get().GetCursorPos();

	//UE_LOG(LogTemp, Error, TEXT("Mouse Move %f"), CursorPosition.X);
	DragDropWindow.Get()->MoveWindowTo(FVector2D(CursorPosition.X  - (DragDropWindowSize.X / 2), CursorPosition.Y - (DragDropWindowSize.Y / 2)));
	return TargetHandler->OnMouseMove();
}

bool FBridgeMessageHandler::OnRawMouseMove(const int32 X, const int32 Y)
{
	return TargetHandler->OnRawMouseMove(X,Y);
}

bool FBridgeMessageHandler::OnCursorSet()
{
	return TargetHandler->OnCursorSet();
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
bool FBridgeMessageHandler::OnControllerAnalog(FGamepadKeyNames::Type KeyName, int32 ControllerId, float AnalogValue)
{
	return TargetHandler->OnControllerAnalog(KeyName, ControllerId, AnalogValue);
}

bool FBridgeMessageHandler::OnControllerButtonPressed(FGamepadKeyNames::Type KeyName, int32 ControllerId, bool IsRepeat)
{
	return TargetHandler->OnControllerButtonPressed(KeyName, ControllerId, IsRepeat);
}

bool FBridgeMessageHandler::OnControllerButtonReleased(FGamepadKeyNames::Type KeyName, int32 ControllerId, bool IsRepeat)
{
	return TargetHandler->OnControllerButtonReleased(KeyName, ControllerId, IsRepeat);
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

bool FBridgeMessageHandler::OnControllerAnalog(FGamepadKeyNames::Type KeyName, FPlatformUserId PlatformUserId, FInputDeviceId InputDeviceId, float AnalogValue)
{
	return TargetHandler->OnControllerAnalog(KeyName, PlatformUserId, InputDeviceId, AnalogValue);
}

bool FBridgeMessageHandler::OnControllerButtonPressed(FGamepadKeyNames::Type KeyName, FPlatformUserId PlatformUserId, FInputDeviceId InputDeviceId, bool IsRepeat)
{
	return TargetHandler->OnControllerButtonPressed(KeyName, PlatformUserId, InputDeviceId, IsRepeat);
}

bool FBridgeMessageHandler::OnControllerButtonReleased(FGamepadKeyNames::Type KeyName, FPlatformUserId PlatformUserId, FInputDeviceId InputDeviceId, bool IsRepeat)
{
	return TargetHandler->OnControllerButtonReleased(KeyName, PlatformUserId, InputDeviceId, IsRepeat);
}

void FBridgeMessageHandler::OnBeginGesture()
{
	TargetHandler->OnBeginGesture();
}

bool FBridgeMessageHandler::OnTouchGesture(EGestureEvent GestureType, const FVector2D& Delta, float WheelDelta, bool bIsDirectionInvertedFromDevice)
{
	return TargetHandler->OnTouchGesture(GestureType, Delta, WheelDelta, bIsDirectionInvertedFromDevice);
}

void FBridgeMessageHandler::OnEndGesture()
{
	TargetHandler->OnEndGesture();
}

bool FBridgeMessageHandler::OnTouchStarted(const TSharedPtr< FGenericWindow >& Window, const FVector2D& Location, float Force, int32 TouchIndex, int32 ControllerId)
{
	return TargetHandler->OnTouchStarted(Window, Location, Force, TouchIndex, ControllerId);
}

bool FBridgeMessageHandler::OnTouchMoved(const FVector2D& Location, float Force, int32 TouchIndex, int32 ControllerId)
{
	return TargetHandler->OnTouchMoved(Location, Force, TouchIndex, ControllerId);
}

bool FBridgeMessageHandler::OnTouchEnded(const FVector2D& Location, int32 TouchIndex, int32 ControllerId)
{
	return TargetHandler->OnTouchEnded(Location, TouchIndex, ControllerId);
}

bool FBridgeMessageHandler::OnTouchForceChanged(const FVector2D& Location, float Force, int32 TouchIndex, int32 ControllerId)
{
	return TargetHandler->OnTouchForceChanged(Location, Force, TouchIndex, ControllerId);
}

bool FBridgeMessageHandler::OnTouchFirstMove(const FVector2D& Location, float Force, int32 TouchIndex, int32 ControllerId)
{
	return TargetHandler->OnTouchFirstMove(Location, Force, TouchIndex, ControllerId);
}

void FBridgeMessageHandler::ShouldSimulateGesture(EGestureEvent Gesture, bool bEnable)
{
	if (TargetHandler.IsValid())
	{
		TargetHandler->ShouldSimulateGesture(Gesture, bEnable);
	}
}

bool FBridgeMessageHandler::OnMotionDetected(const FVector& Tilt, const FVector& RotationRate, const FVector& Gravity, const FVector& Acceleration, int32 ControllerId)
{
	if (TargetHandler.IsValid())
	{
		return TargetHandler->OnMotionDetected(Tilt, RotationRate, Gravity, Acceleration, ControllerId);
	}

	return false;
}

bool FBridgeMessageHandler::OnSizeChanged(const TSharedRef< FGenericWindow >& Window, const int32 Width, const int32 Height, bool bWasMinimized /*= false*/)
{
	if (TargetHandler.IsValid())
	{
		return TargetHandler->OnSizeChanged(Window, Width, Height, bWasMinimized);
	}

	return false;
}

void FBridgeMessageHandler::OnOSPaint(const TSharedRef<FGenericWindow>& Window)
{
	if (TargetHandler.IsValid())
	{
		TargetHandler->OnOSPaint(Window);
	}
}

FWindowSizeLimits FBridgeMessageHandler::GetSizeLimitsForWindow(const TSharedRef<FGenericWindow>& Window) const
{
	if (TargetHandler.IsValid())
	{
		return TargetHandler->GetSizeLimitsForWindow(Window);
	}

	return FWindowSizeLimits();
}

void FBridgeMessageHandler::OnResizingWindow(const TSharedRef< FGenericWindow >& Window)
{
	if (TargetHandler.IsValid())
	{
		TargetHandler->OnResizingWindow(Window);
	}
}

bool FBridgeMessageHandler::BeginReshapingWindow(const TSharedRef< FGenericWindow >& Window)
{
	if (TargetHandler.IsValid())
	{
		return TargetHandler->BeginReshapingWindow(Window);
	}

	return true;
}

void FBridgeMessageHandler::FinishedReshapingWindow(const TSharedRef< FGenericWindow >& Window)
{
	if (TargetHandler.IsValid())
	{
		TargetHandler->FinishedReshapingWindow(Window);
	}
}

void FBridgeMessageHandler::HandleDPIScaleChanged(const TSharedRef< FGenericWindow >& Window)
{
	if (TargetHandler.IsValid())
	{
		return TargetHandler->HandleDPIScaleChanged(Window);
	}
}

void FBridgeMessageHandler::OnMovedWindow(const TSharedRef< FGenericWindow >& Window, const int32 X, const int32 Y)
{
	if (TargetHandler.IsValid())
	{
		return TargetHandler->HandleDPIScaleChanged(Window);
	}
}

bool FBridgeMessageHandler::OnWindowActivationChanged(const TSharedRef< FGenericWindow >& Window, const EWindowActivation ActivationType)
{
	if (TargetHandler.IsValid())
	{
		return TargetHandler->OnWindowActivationChanged(Window, ActivationType);
	}

	return false;
}

bool FBridgeMessageHandler::OnApplicationActivationChanged(const bool IsActive)
{
	if (TargetHandler.IsValid())
	{
		return TargetHandler->OnApplicationActivationChanged(IsActive);
	}

	return false;
}

bool FBridgeMessageHandler::OnConvertibleLaptopModeChanged()
{
	if (TargetHandler.IsValid())
	{
		return TargetHandler->OnConvertibleLaptopModeChanged();
	}

	return false;
}

EWindowZone::Type FBridgeMessageHandler::GetWindowZoneForPoint(const TSharedRef< FGenericWindow >& Window, const int32 X, const int32 Y)
{
	if (TargetHandler.IsValid())
	{
		return TargetHandler->GetWindowZoneForPoint(Window, X, Y);
	}

	return EWindowZone::NotInWindow;
}

void FBridgeMessageHandler::OnWindowClose(const TSharedRef< FGenericWindow >& Window)
{
	if (TargetHandler.IsValid())
	{
		TargetHandler->OnWindowClose(Window);
	}
}

EDropEffect::Type FBridgeMessageHandler::OnDragEnterText(const TSharedRef< FGenericWindow >& Window, const FString& Text)
{
	if (TargetHandler.IsValid())
	{
		return TargetHandler->OnDragEnterText(Window, Text);
	}

	return EDropEffect::None;
}

EDropEffect::Type FBridgeMessageHandler::OnDragEnterFiles(const TSharedRef< FGenericWindow >& Window, const TArray< FString >& Files)
{
	if (TargetHandler.IsValid())
	{
		return TargetHandler->OnDragEnterFiles(Window, Files);
	}

	return EDropEffect::None;
}

EDropEffect::Type FBridgeMessageHandler::OnDragEnterExternal(const TSharedRef< FGenericWindow >& Window, const FString& Text, const TArray< FString >& Files)
{
	if (TargetHandler.IsValid())
	{
		return TargetHandler->OnDragEnterExternal(Window, Text, Files);
	}

	return EDropEffect::None;
}

EDropEffect::Type FBridgeMessageHandler::OnDragOver(const TSharedPtr< FGenericWindow >& Window)
{
	if (TargetHandler.IsValid())
	{
		return TargetHandler->OnDragOver(Window);
	}

	return EDropEffect::None;
}

void FBridgeMessageHandler::OnDragLeave(const TSharedPtr< FGenericWindow >& Window)
{
	if (TargetHandler.IsValid())
	{
		TargetHandler->OnDragLeave(Window);
	}
}

EDropEffect::Type FBridgeMessageHandler::OnDragDrop(const TSharedPtr< FGenericWindow >& Window)
{
	if (TargetHandler.IsValid())
	{
		return TargetHandler->OnDragDrop(Window);
	}

	return EDropEffect::None;
}

bool FBridgeMessageHandler::OnWindowAction(const TSharedRef< FGenericWindow >& Window, const EWindowAction::Type InActionType)
{
	if (TargetHandler.IsValid())
	{
		return TargetHandler->OnWindowAction(Window, InActionType);
	}

	return true;
}

void FBridgeMessageHandler::SetCursorPos(const FVector2D& MouseCoordinate)
{
	if (TargetHandler.IsValid())
	{
		TargetHandler->SetCursorPos(MouseCoordinate);
	}
}

void FBridgeMessageHandler::SignalSystemDPIChanged(const TSharedRef< FGenericWindow >& Window)
{
	if (TargetHandler.IsValid())
	{
		TargetHandler->SignalSystemDPIChanged(Window);
	}
}

void FBridgeMessageHandler::OnInputLanguageChanged()
{
	if (TargetHandler.IsValid())
	{
		TargetHandler->OnInputLanguageChanged();
	}
}
