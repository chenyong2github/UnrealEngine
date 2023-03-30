// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "HAL/UnrealMemory.h"
#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Containers/Map.h"
#include "Math/Vector2D.h"
#include "Templates/SharedPointer.h"
#include "Misc/CoreMisc.h"
#include "GenericPlatform/GenericWindow.h"
#include "GenericPlatform/GenericApplicationMessageHandler.h"
#include "GenericPlatform/GenericWindowDefinition.h"
#include "GenericPlatform/GenericApplication.h"
#include "GenericPlatform/IInputInterface.h"
#include "Null/NullWindow.h"
#include "Null/NullCursor.h"

class IInputDevice;

struct FNullPlatformDisplayMetrics : public FDisplayMetrics
{
	APPLICATIONCORE_API static void RebuildDisplayMetrics(struct FDisplayMetrics& OutDisplayMetrics);
};

/**
 * An implementation of GenericApplication specifically for use when rendering off screen.
 * This application has no platform backing so instead keeps track of its associated NullWindows itself.
 */
class APPLICATIONCORE_API FNullApplication : public GenericApplication, public FSelfRegisteringExec, public IInputInterface
{
public:
	static FNullApplication* CreateNullApplication();

	static void MoveWindowTo(FGenericWindow* Window, const int32 X, const int32 Y);

	static void OnSizeChanged(FGenericWindow* Window, const int32 Width, const int32 Height);

	static void GetFullscreenInfo(int32& X, int32& Y, int32& Width, int32& Height);

	static void ShowWindow(FGenericWindow* Window);

	static void HideWindow(FGenericWindow* Window);

	static void DestroyWindow(FGenericWindow* Window);

public:
	virtual ~FNullApplication();

	virtual void DestroyApplication() override;

	// FSelfRegisteringExec
	virtual bool Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) override;

	TSharedPtr<FNullWindow> FindWindowByPtr(FGenericWindow* WindowToFind);

	void ActivateWindow(const TSharedPtr<FNullWindow>& Window);

public:
	virtual void SetMessageHandler(const TSharedRef<class FGenericApplicationMessageHandler>& InMessageHandler) override;

	virtual void PollGameDeviceState(const float TimeDelta) override;

	virtual void PumpMessages(const float TimeDelta) override;

	virtual void ProcessDeferredEvents(const float TimeDelta) override;

	virtual TSharedRef<FGenericWindow> MakeWindow() override;

	virtual void InitializeWindow(const TSharedRef<FGenericWindow>& Window, const TSharedRef<FGenericWindowDefinition>& InDefinition, const TSharedPtr<FGenericWindow>& InParent, const bool bShowImmediately) override;

	void DestroyWindow(TSharedRef<FNullWindow> WindowToRemove);

	virtual void SetCapture(const TSharedPtr<FGenericWindow>& InWindow) override;

	virtual void* GetCapture(void) const override;

	virtual void SetHighPrecisionMouseMode(const bool Enable, const TSharedPtr<FGenericWindow>& InWindow) override;

	virtual bool IsUsingHighPrecisionMouseMode() const override { return bUsingHighPrecisionMouseInput; }

	virtual bool IsGamepadAttached() const override;

	virtual FModifierKeysState GetModifierKeys() const override;

	virtual FPlatformRect GetWorkArea(const FPlatformRect& CurrentWindow) const override;

	void SetWorkArea(const FPlatformRect& NewWorkArea);

	virtual EWindowTransparency GetWindowTransparencySupport() const override
	{
		return EWindowTransparency::PerWindow;
	}

	virtual bool IsCursorDirectlyOverSlateWindow() const override;

	virtual TSharedPtr<FGenericWindow> GetWindowUnderCursor() override;

	virtual bool IsMouseAttached() const override { return true; }

private:
	FNullApplication();

	/** Handles "Cursor" exec commands" */
	bool HandleCursorCommand(const TCHAR* Cmd, FOutputDevice& Ar);

	/** Handles "Window" exec commands" */
	bool HandleWindowCommand(const TCHAR* Cmd, FOutputDevice& Ar);

	/** Handles parsing the work area resolution from the command line */
	bool ParseResolution(const TCHAR* InResolution, uint32& OutX, uint32& OutY);

public:
	virtual IInputInterface* GetInputInterface() override
	{
		return this;
	}

	// IInputInterface overrides
	virtual void SetForceFeedbackChannelValue(int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value) override;
	virtual void SetForceFeedbackChannelValues(int32 ControllerId, const FForceFeedbackValues& Values) override;
	virtual void SetHapticFeedbackValues(int32 ControllerId, int32 Hand, const FHapticFeedbackValues& Values) override;
	virtual void SetLightColor(int32 ControllerId, FColor Color) override {}
	virtual void ResetLightColor(int32 ControllerId) override {}

private:
	TArray<TSharedRef<FNullWindow>> Windows;

	/** List of input devices implemented in external modules. */
	TArray<TSharedPtr<class IInputDevice>> ExternalInputDevices;
	bool bHasLoadedInputPlugins;

	/** Using high precision mouse input */
	bool bUsingHighPrecisionMouseInput;

	/** Window that we think has been activated last. */
	TSharedPtr<FNullWindow> CurrentlyActiveWindow;

	/** Window that we think has been previously active. */
	TSharedPtr<FNullWindow> PreviousActiveWindow;

	/** The virtual work area*/
	FPlatformRect WorkArea;
};

extern FNullApplication* NullApplication;
