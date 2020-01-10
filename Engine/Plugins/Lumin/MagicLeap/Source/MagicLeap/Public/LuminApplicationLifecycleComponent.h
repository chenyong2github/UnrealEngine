// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ApplicationLifecycleComponent.h"
#include "LuminApplicationLifecycleComponent.generated.h"

/** Component to handle receiving notifications from the LuminOS about application state (activated, suspended, termination, standby etc). */
UCLASS(ClassGroup=Utility, HideCategories=(Activation, "Components|Activation", Collision), meta=(BlueprintSpawnableComponent))
class MAGICLEAP_API ULuminApplicationLifecycleComponent : public UApplicationLifecycleComponent
{
	GENERATED_BODY()

public:
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FLuminApplicationLifetimeDelegate);

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

private:
	/** Native handlers that get registered with the actual FLuminDelegates, and then proceed to broadcast to the delegates above */
	void DeviceHasReactivatedDelegate_Handler() { DeviceHasReactivatedDelegate.Broadcast(); }
	void DeviceWillEnterRealityModeDelegate_Handler() { DeviceWillEnterRealityModeDelegate.Broadcast(); }
	void DeviceWillGoInStandbyDelegate_Handler() { DeviceWillGoInStandbyDelegate.Broadcast(); }
};
