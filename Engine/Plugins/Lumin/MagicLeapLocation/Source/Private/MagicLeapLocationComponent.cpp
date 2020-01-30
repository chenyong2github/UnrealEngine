// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapLocationComponent.h"
#include "MagicLeapLocationPlugin.h"

UMagicLeapLocationComponent::UMagicLeapLocationComponent()
{
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.bCanEverTick = true;
}

UMagicLeapLocationComponent::~UMagicLeapLocationComponent()
{ }

bool UMagicLeapLocationComponent::GetLastCoarseLocation(FLocationData& OutLocation)
{
	return GET_MAGIC_LEAP_LOCATION_PLUGIN()->GetLastCoarseLocation(OutLocation);
}

bool UMagicLeapLocationComponent::GetLastCoarseLocationAsync()
{
	return GET_MAGIC_LEAP_LOCATION_PLUGIN()->GetLastCoarseLocationAsync(OnGotCoarseLocation);
}

bool UMagicLeapLocationComponent::GetLastCoarseLocationOnSphere(float InRadius, FVector& OutLocation)
{
	return GET_MAGIC_LEAP_LOCATION_PLUGIN()->GetLastCoarseLocationOnSphere(InRadius, OutLocation);
}

bool UMagicLeapLocationComponent::GetLastCoarseLocationOnSphereAsync(float InRadius)
{
	return GET_MAGIC_LEAP_LOCATION_PLUGIN()->GetLastCoarseLocationOnSphereAsync(OnGotCoarseLocationOnSphere, InRadius);
}
