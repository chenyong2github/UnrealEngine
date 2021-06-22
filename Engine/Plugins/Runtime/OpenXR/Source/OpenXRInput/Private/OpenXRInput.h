// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "OpenXRCore.h"
#include "GenericPlatform/IInputInterface.h"
#include "XRMotionControllerBase.h"
#include "IOpenXRInputPlugin.h"
#include "IInputDevice.h"
#include "IHapticDevice.h"

class FOpenXRHMD;
struct FInputActionKeyMapping;
struct FInputAxisKeyMapping;

class FOpenXRInputPlugin : public IOpenXRInputPlugin
{
public:
	struct FOpenXRAction
	{
		XrActionSet		Set;
		XrActionType	Type;
		FName			Name;
		XrAction		Handle;

		TMap<TPair<XrPath, XrPath>, FName> KeyMap;

		FOpenXRAction(XrActionSet InActionSet, XrActionType InActionType, const FName& InName, const TArray<XrPath>& SubactionPaths);
	};

	struct FOpenXRController
	{
		XrActionSet		ActionSet;
		XrPath			UserPath;
		XrAction		GripAction;
		XrAction		AimAction;
		XrAction		VibrationAction;
		int32			GripDeviceId;
		int32			AimDeviceId;

		FOpenXRController(XrActionSet InActionSet, XrPath InUserPath, const char* InName);

		void AddActionDevices(FOpenXRHMD* HMD);
	};

	struct FInteractionProfile
	{
	public:
		bool HasHaptics;
		XrPath Path;
		TArray<XrActionSuggestedBinding> Bindings;

		FInteractionProfile(XrPath InProfile, bool InHasHaptics);
	};

	class FOpenXRInput : public IInputDevice, public FXRMotionControllerBase, public IHapticDevice, public TSharedFromThis<FOpenXRInput>
	{
	public:
		FOpenXRInput(FOpenXRHMD* HMD);
		virtual ~FOpenXRInput();

		// IInputDevice overrides
		virtual void Tick(float DeltaTime) override;
		virtual void SendControllerEvents() override;
		virtual void SetMessageHandler(const TSharedRef< FGenericApplicationMessageHandler >& InMessageHandler) override;
		virtual bool Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) override;
		virtual void SetChannelValue(int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value) override;
		virtual void SetChannelValues(int32 ControllerId, const FForceFeedbackValues &values) override;

		// IMotionController overrides
		virtual FName GetMotionControllerDeviceTypeName() const override;
		virtual bool GetControllerOrientationAndPosition(const int32 ControllerIndex, const FName MotionSource, FRotator& OutOrientation, FVector& OutPosition, float WorldToMetersScale) const override;
		virtual bool GetControllerOrientationAndPositionForTime(const int32 ControllerIndex, const FName MotionSource, FTimespan Time, bool& OutTimeWasUsed, FRotator& OutOrientation, FVector& OutPosition, bool& OutbProvidedLinearVelocity, FVector& OutLinearVelocity, bool& OutbProvidedAngularVelocity, FVector& OutAngularVelocityRadPerSec, float WorldToMetersScale) const override;
		virtual ETrackingStatus GetControllerTrackingStatus(const int32 ControllerIndex, const FName MotionSource) const override;
		virtual bool GetControllerOrientationAndPosition(const int32 ControllerIndex, const EControllerHand DeviceHand, FRotator& OutOrientation, FVector& OutPosition, float WorldToMetersScale) const override { check(false); return false; }
		virtual ETrackingStatus GetControllerTrackingStatus(const int32 ControllerIndex, const EControllerHand DeviceHand) const override { check(false); return ETrackingStatus::NotTracked; }
		virtual void EnumerateSources(TArray<FMotionControllerSource>& SourcesOut) const override;

		// IHapticDevice overrides
		IHapticDevice* GetHapticDevice() override { return (IHapticDevice*)this; }
		virtual void SetHapticFeedbackValues(int32 ControllerId, int32 Hand, const FHapticFeedbackValues& Values) override;

		virtual void GetHapticFrequencyRange(float& MinFrequency, float& MaxFrequency) const override;
		virtual float GetHapticAmplitudeScale() const override;

	private:
		static const XrDuration MaxFeedbackDuration = 2500000000; // 2.5 seconds

		FOpenXRHMD* OpenXRHMD;

		TArray<XrActiveActionSet> ActionSets;
		TArray<XrPath> SubactionPaths;
		TArray<FOpenXRAction> Actions;
		TMap<EControllerHand, FOpenXRController> Controllers;
		TMap<FName, EControllerHand> MotionSourceToControllerHandMap;
		XrAction GetActionForMotionSource(FName MotionSource) const;
		int32 GetDeviceIDForMotionSource(FName MotionSource) const;
		XrPath GetUserPathForMotionSource(FName MotionSource) const;
		bool IsOpenXRInputSupportedMotionSource(const FName MotionSource) const;
		bool bActionsBound;

		void BuildActions();
		void DestroyActions();

		template<typename T>
		int32 SuggestBindings(XrInstance Instance, FOpenXRAction& Action, const TArray<T>& Mappings, TMap<FString, FInteractionProfile>& Profiles);

		/** handler to send all messages to */
		TSharedRef<FGenericApplicationMessageHandler> MessageHandler;
	};

	FOpenXRInputPlugin();
	virtual ~FOpenXRInputPlugin();

	virtual void StartupModule() override;
	virtual TSharedPtr< class IInputDevice > CreateInputDevice(const TSharedRef< FGenericApplicationMessageHandler >& InMessageHandler) override;

private:
	FOpenXRHMD* GetOpenXRHMD() const;

private:
	TSharedPtr<FOpenXRInput> InputDevice;
};
