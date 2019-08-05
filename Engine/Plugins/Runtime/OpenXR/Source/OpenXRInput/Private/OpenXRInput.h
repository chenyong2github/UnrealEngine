// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GenericPlatform/IInputInterface.h"
#include "XRMotionControllerBase.h"
#include "IOpenXRInputPlugin.h"
#include "IInputDevice.h"
#include "IHapticDevice.h"

#include <openxr/openxr.h>

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
		FName			ActionKey;
		XrAction		Handle;

		FOpenXRAction(XrActionSet InActionSet, XrActionType InActionType, const FName& InName);

		FOpenXRAction(XrActionSet InActionSet, const FName& InName, const FName& InActionKey)
			: FOpenXRAction(InActionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, InName)
		{
			ActionKey = InActionKey;
		}

		FOpenXRAction(XrActionSet InSet, const FInputActionKeyMapping& InActionKey);
		FOpenXRAction(XrActionSet InSet, const FInputAxisKeyMapping& InAxisKey);
	};

	struct FOpenXRController
	{
		XrActionSet		ActionSet;
		XrAction		Action;
		XrAction		VibrationAction;
		int32			DeviceId;

		FOpenXRController(FOpenXRHMD* HMD, XrActionSet InActionSet, const char* InName);
	};

	class FOpenXRInput : public IInputDevice, public FXRMotionControllerBase, public IHapticDevice
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
		virtual bool GetControllerOrientationAndPosition(const int32 ControllerIndex, const EControllerHand DeviceHand, FRotator& OutOrientation, FVector& OutPosition, float WorldToMetersScale) const override;
		virtual ETrackingStatus GetControllerTrackingStatus(const int32 ControllerIndex, const EControllerHand DeviceHand) const override;

		// IHapticDevice overrides
		IHapticDevice* GetHapticDevice() override { return (IHapticDevice*)this; }
		virtual void SetHapticFeedbackValues(int32 ControllerId, int32 Hand, const FHapticFeedbackValues& Values) override;

		virtual void GetHapticFrequencyRange(float& MinFrequency, float& MaxFrequency) const override;
		virtual float GetHapticAmplitudeScale() const override;

	private:
		static const XrDuration MaxFeedbackDuration = 2500000000; // 2.5 seconds

		FOpenXRHMD* OpenXRHMD;

		TArray<XrActiveActionSet> ActionSets;
		TArray<FOpenXRAction> Actions;
		TMap<EControllerHand, FOpenXRController> Controllers;
		TMap<FName, XrPath> InteractionMappings;

		bool bActionsBound;

		template<typename T>
		void AddAction(XrActionSet ActionSet, const TArray<T>& Mappings, TArray<XrActionSuggestedBinding>& OutSuggestedBindings);

		/** handler to send all messages to */
		TSharedRef<FGenericApplicationMessageHandler> MessageHandler;
	};

	FOpenXRInputPlugin();
	virtual ~FOpenXRInputPlugin();

	FOpenXRHMD* GetOpenXRHMD() const;

	virtual void StartupModule() override;
	virtual TSharedPtr< class IInputDevice > CreateInputDevice(const TSharedRef< FGenericApplicationMessageHandler >& InMessageHandler) override;

private:
	TSharedPtr<FOpenXRInput> InputDevice;
};
