// Copyright Epic Games, Inc. All Rights Reserved.
#include "OculusEventComponent.h"
#include "OculusHMD.h"
#include "OculusDelegates.h"

void UOculusEventComponent::OnRegister()
{
	Super::OnRegister();

	FOculusEventDelegates::OculusDisplayRefreshRateChanged.AddUObject(this, &UOculusEventComponent::OculusDisplayRefreshRateChanged_Handler);
}

void UOculusEventComponent::OnUnregister()
{
	Super::OnUnregister();

	FOculusEventDelegates::OculusDisplayRefreshRateChanged.RemoveAll(this);
}
