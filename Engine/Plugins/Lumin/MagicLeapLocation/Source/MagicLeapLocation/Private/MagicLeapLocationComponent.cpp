// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapLocationComponent.h"
#include "MagicLeapLocationPlugin.h"

bool UMagicLeapLocationComponent::GetLastLocation(FMagicLeapLocationData& OutLocation, bool bUseFineLocation)
{
	return GetMagicLeapLocationPlugin().GetLastLocation(OutLocation, bUseFineLocation);
}

bool UMagicLeapLocationComponent::GetLastLocationAsync(bool bUseFineLocation)
{
	return GetMagicLeapLocationPlugin().GetLastLocationAsync(OnGotLocation, bUseFineLocation);
}

bool UMagicLeapLocationComponent::GetLastLocationOnSphere(float InRadius, FVector& OutLocation, bool bUseFineLocation)
{
	return GetMagicLeapLocationPlugin().GetLastLocationOnSphere(InRadius, OutLocation, bUseFineLocation);
}

bool UMagicLeapLocationComponent::GetLastLocationOnSphereAsync(float InRadius, bool bUseFineLocation)
{
	return GetMagicLeapLocationPlugin().GetLastLocationOnSphereAsync(OnGotLocationOnSphere, InRadius, bUseFineLocation);
}
