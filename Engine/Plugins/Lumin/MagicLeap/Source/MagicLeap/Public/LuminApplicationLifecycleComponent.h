// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ApplicationLifecycleComponent.h"
#include "Lumin/CAPIShims/LuminAPILifecycle.h"
#include "LuminApplicationLifecycleComponent.generated.h"

UENUM(BlueprintType)
enum class EFocusLostReason : uint8
{
	/** Value returned when focus is lost due to an unknown event */
	EFocusLostReason_Invalid,
	/** Value returned when focus is lost due to a system dialog. */
	EFocusLostReason_System,
};

/** Component to handle receiving notifications from the LuminOS about application state (activated, suspended, termination, standby etc). */
UCLASS(ClassGroup=Utility, HideCategories=(Activation, "Components|Activation", Collision), meta=(BlueprintSpawnableComponent))
class MAGICLEAP_API ULuminApplicationLifecycleComponent : public UApplicationLifecycleComponent
{
	GENERATED_BODY()

public:
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FLuminApplicationLifetimeDelegate);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLuminApplicationLifetimeFocusLostDelegate, EFocusLostReason, reason);

	void OnRegister() override;
	void OnUnregister() override;

	/**
		This event is called when the device has transitioned to the active mode
		from reality or standby.
		This is triggered when the device comes out of the reality mode
		or when the wearable is back on head and is no longer in standby mode.
	*/
	UPROPERTY(BlueprintAssignable)
	FLuminApplicationLifetimeDelegate DeviceHasReactivatedDelegate;  

	/** This event is called when the device has transitioned to the reality mode. */
	UPROPERTY(BlueprintAssignable)
	FLuminApplicationLifetimeDelegate DeviceWillEnterRealityModeDelegate;  

	/**
		This callback is called when the device has transitioned to the standby mode.
		This is triggered when the wearable is off head.
	*/
	UPROPERTY(BlueprintAssignable)
	FLuminApplicationLifetimeDelegate DeviceWillGoInStandbyDelegate;  

	/** This events is called when focus has been lost, usually because a system dialog has been displayed */
	UPROPERTY(BlueprintAssignable)
	FLuminApplicationLifetimeFocusLostDelegate FocusLostDelegate;

	/** This events is called when focus has been gained, usually on startup or after a system dialog has been closed */
	UPROPERTY(BlueprintAssignable)
	FLuminApplicationLifetimeDelegate FocusGainedDelegate;

private:
	/** Native handlers that get registered with the actual FLuminDelegates, and then proceed to broadcast to the delegates above */
	void DeviceHasReactivatedDelegate_Handler() { DeviceHasReactivatedDelegate.Broadcast(); }
	void DeviceWillEnterRealityModeDelegate_Handler() { DeviceWillEnterRealityModeDelegate.Broadcast(); }
	void DeviceWillGoInStandbyDelegate_Handler() { DeviceWillGoInStandbyDelegate.Broadcast(); }
#if WITH_MLSDK
	EFocusLostReason MLFocusLostToUEFocusLostReason(MLLifecycleFocusLostReason InMLFocusLostReason)
	{
		switch (InMLFocusLostReason)
		{
		case MLLifecycleFocusLostReason_System:
			return EFocusLostReason::EFocusLostReason_System;
		case MLLifecycleFocusLostReason_Invalid:
		default:
			return EFocusLostReason::EFocusLostReason_Invalid;
		}
	};

	void FocusLostDelegate_Handler(MLLifecycleFocusLostReason reason) { FocusLostDelegate.Broadcast(MLFocusLostToUEFocusLostReason(reason)); }
#endif
	void FocusGainedDelegate_Handler() { FocusGainedDelegate.Broadcast(); }
};
