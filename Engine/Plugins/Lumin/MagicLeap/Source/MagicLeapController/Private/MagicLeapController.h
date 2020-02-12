// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GenericPlatform/IInputInterface.h"
#include "IInputDevice.h"
#include "IMagicLeapTrackerEntity.h"
#include "IMotionController.h"
#include "XRMotionControllerBase.h"
#include "IMagicLeapControllerPlugin.h"
#include <Containers/Queue.h>
#include "Misc/ScopeLock.h"
#include "Lumin/CAPIShims/LuminAPIController.h"
#include "Lumin/CAPIShims/LuminAPIInput.h"

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

#include "MagicLeapControllerKeys.h"
#include "MagicLeapInputState.h"

//function to force the linker to include this cpp
void MagicLeapTestReferenceFunction();

class IMagicLeapTouchpadGestures;

/**
 * MagicLeap Motion Controller
 */
class FMagicLeapController : public IInputDevice, public IMagicLeapTrackerEntity, public FXRMotionControllerBase
{
private:
#if WITH_MLSDK
	class FControllerMapper
	{
	friend class FMagicLeapController;
	private:
		TMap<FName, int32> MotionSourceToInputControllerIndex;
		FName InputControllerIndexToMotionSource[MLInput_MaxControllers];
		TMap<EControllerHand, FName> HandToMotionSource;
		TMap<FName, EControllerHand> MotionSourceToHand;
		FCriticalSection CriticalSection;
		EControllerHand DefaultInputControllerIndexToHand[MLInput_MaxControllers];

	protected:
		void UpdateMotionSourceInputIndexPairing(const MLInputControllerState ControllerState[MLInput_MaxControllers]);
	public:
		FControllerMapper();

		void MapHandToMotionSource(EControllerHand Hand, FName MotionSource);

		FName GetMotionSourceForHand(EControllerHand Hand) const;
		EControllerHand GetHandForMotionSource(FName MotionSource) const;

		FName GetMotionSourceForInputControllerIndex(uint8 controller_id) const;
		uint8 GetInputControllerIndexForMotionSource(FName MotionSource) const;

		EControllerHand GetHandForInputControllerIndex(uint8 controller_id) const;
		uint8 GetInputControllerIndexForHand(EControllerHand Hand) const;
		EMagicLeapControllerType MotionSourceToControllerType(FName InMotionSource);

		void SwapHands();
	};
#endif //WITH_MLSDK
public:
	FMagicLeapController(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler);
	virtual ~FMagicLeapController();

	/** IInputDevice interface */
	void Tick(float DeltaTime) override;
	void SendControllerEvents() override;
	void SetMessageHandler(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler) override;
	bool Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) override;
	bool IsGamepadAttached() const override;

	/** IMagicLeapTrackerEntity interface */
	void CreateEntityTracker() override;
	void DestroyEntityTracker() override;

	/** IMotionController interface */
	bool GetControllerOrientationAndPosition(const int32 ControllerIndex, const FName MotionSource, FRotator& OutOrientation, FVector& OutPosition, float WorldToMetersScale) const override;
	ETrackingStatus GetControllerTrackingStatus(const int32 ControllerIndex, const FName MotionSource) const override;
	FName GetMotionControllerDeviceTypeName() const override;
	void EnumerateSources(TArray<FMotionControllerSource>& SourcesOut) const override;

	/** IInputDevice interface */
	void SetChannelValue(int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value) override { }
	void SetChannelValues(int32 ControllerId, const FForceFeedbackValues &values) override { }

	EMagicLeapControllerTrackingMode GetControllerTrackingMode();
	bool SetControllerTrackingMode(EMagicLeapControllerTrackingMode TrackingMode);

	void RegisterTouchpadGestureReceiver(IMagicLeapTouchpadGestures* Receiver);
	void UnregisterTouchpadGestureReceiver(IMagicLeapTouchpadGestures* Receiver);

	bool PlayLEDPattern(FName MotionSource, EMagicLeapControllerLEDPattern LEDPattern, EMagicLeapControllerLEDColor LEDColor, float DurationInSec);
	bool PlayLEDEffect(FName MotionSource, EMagicLeapControllerLEDEffect LEDEffect, EMagicLeapControllerLEDSpeed LEDSpeed, EMagicLeapControllerLEDPattern LEDPattern, EMagicLeapControllerLEDColor LEDColor, float DurationInSec);
	bool PlayHapticPattern(FName MotionSource, EMagicLeapControllerHapticPattern HapticPattern, EMagicLeapControllerHapticIntensity Intensity);

	bool IsMLControllerConnected(FName MotionSource) const;

#if WITH_MLSDK
	// Has to be public so button functions can use it
	FControllerMapper ControllerMapper;
#endif //WITH_MLSDK

	bool GetControllerOrientationAndPosition(const int32 ControllerIndex, const EControllerHand DeviceHand, FRotator& OutOrientation, FVector& OutPosition, float WorldToMetersScale) const override;
	ETrackingStatus GetControllerTrackingStatus(const int32 ControllerIndex, const EControllerHand DeviceHand) const override;
	EMagicLeapControllerType GetMLControllerType(EControllerHand Hand) const;
	bool PlayControllerLED(EControllerHand Hand, EMagicLeapControllerLEDPattern LEDPattern, EMagicLeapControllerLEDColor LEDColor, float DurationInSec);
	bool PlayControllerLEDEffect(EControllerHand Hand, EMagicLeapControllerLEDEffect LEDEffect, EMagicLeapControllerLEDSpeed LEDSpeed, EMagicLeapControllerLEDPattern LEDPattern, EMagicLeapControllerLEDColor LEDColor, float DurationInSec);
	bool PlayControllerHapticFeedback(EControllerHand Hand, EMagicLeapControllerHapticPattern HapticPattern, EMagicLeapControllerHapticIntensity Intensity);

private:
	void UpdateTrackerData();
	void UpdateControllerStateFromInputTracker(const class IMagicLeapPlugin& MLPlugin, FName MotionSource);
	void UpdateControllerStateFromControllerTracker(const class IMagicLeapPlugin& MLPlugin, FName MotionSource);
	void AddKeys();
	void ReadConfigParams();
	void InitializeInputCallbacks();
	void SendControllerEventsForHand(EControllerHand Hand);

	TSharedPtr<FGenericApplicationMessageHandler> MessageHandler;
	int32 DeviceIndex;

#if WITH_MLSDK
	MLHandle InputTracker;
	MLHandle ControllerTracker;
	MLInputControllerDof ControllerDof;
	EMagicLeapControllerTrackingMode TrackingMode;
	MLInputControllerState InputControllerState[MLInput_MaxControllers];
	MLControllerSystemState ControllerSystemState;
	MLInputControllerCallbacks InputControllerCallbacks;
	TMap<FName, FMagicLeapControllerState> CurrMotionSourceControllerState;
	TMap<FName, FMagicLeapControllerState> PrevMotionSourceControllerState;

	void EnqueueButton(EControllerHand ControllerHand, MLInputControllerButton Button, bool bIsPressed);
#endif //WITH_MLSDK

	bool bIsInputStateValid;

	float TriggerKeyIsConsideredPressed;
	float TriggerKeyIsConsideredReleased;

	TArray<IMagicLeapTouchpadGestures*> TouchpadGestureReceivers;

	TQueue<TPair<FName, bool>> PendingButtonEvents;
};

DEFINE_LOG_CATEGORY_STATIC(LogMagicLeapController, Display, All);
