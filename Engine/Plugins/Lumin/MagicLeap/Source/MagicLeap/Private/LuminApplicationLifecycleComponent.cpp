// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
#endif // PLATFORM_LUMIN
}

void ULuminApplicationLifecycleComponent::OnUnregister()
{
	Super::OnUnregister();

#if PLATFORM_LUMIN
	FLuminDelegates::DeviceHasReactivatedDelegate.RemoveAll(this);
	FLuminDelegates::DeviceWillEnterRealityModeDelegate.RemoveAll(this);
	FLuminDelegates::DeviceWillGoInStandbyDelegate.RemoveAll(this);
#endif // PLATFORM_LUMIN
}
