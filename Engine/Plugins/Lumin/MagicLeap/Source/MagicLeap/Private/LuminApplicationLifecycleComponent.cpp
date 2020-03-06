// Copyright Epic Games, Inc. All Rights Reserved.

#include "LuminApplicationLifecycleComponent.h"
#if PLATFORM_LUMIN
#include "Lumin/LuminPlatformDelegates.h"
#endif // PLATFORM_LUMIN

void ULuminApplicationLifecycleComponent::OnRegister()
{
	Super::OnRegister();

#if PLATFORM_LUMIN
	FLuminDelegates::DeviceHasReactivatedDelegate.AddUObject(this, &ULuminApplicationLifecycleComponent::DeviceHasReactivatedDelegate_Handler);
	FLuminDelegates::DeviceWillEnterRealityModeDelegate.AddUObject(this, &ULuminApplicationLifecycleComponent::DeviceWillEnterRealityModeDelegate_Handler);
	FLuminDelegates::DeviceWillGoInStandbyDelegate.AddUObject(this, &ULuminApplicationLifecycleComponent::DeviceWillGoInStandbyDelegate_Handler);
#if WITH_MLSDK
	FLuminDelegates::FocusLostDelegate.AddUObject(this, &ULuminApplicationLifecycleComponent::FocusLostDelegate_Handler);
#endif
	FLuminDelegates::FocusGainedDelegate.AddUObject(this, &ULuminApplicationLifecycleComponent::FocusGainedDelegate_Handler);
#endif // PLATFORM_LUMIN
}

void ULuminApplicationLifecycleComponent::OnUnregister()
{
	Super::OnUnregister();

#if PLATFORM_LUMIN
	FLuminDelegates::DeviceHasReactivatedDelegate.RemoveAll(this);
	FLuminDelegates::DeviceWillEnterRealityModeDelegate.RemoveAll(this);
	FLuminDelegates::DeviceWillGoInStandbyDelegate.RemoveAll(this);
#if WITH_MLSDK
	FLuminDelegates::FocusLostDelegate.RemoveAll(this);
#endif
	FLuminDelegates::FocusGainedDelegate.RemoveAll(this);
#endif // PLATFORM_LUMIN
}
