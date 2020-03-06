// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapLocationFunctionLibrary.h"
#include "MagicLeapLocationPlugin.h"

bool UMagicLeapLocationFunctionLibrary::GetLastLocation(FMagicLeapLocationData& OutLocation, bool bUseFineLocation)
{
	return GetMagicLeapLocationPlugin().GetLastLocation(OutLocation, bUseFineLocation);
}

bool UMagicLeapLocationFunctionLibrary::GetLastLocationAsync(const FMagicLeapLocationResultDelegate& InResultDelegate, bool bUseFineLocation)
{
	FMagicLeapLocationResultDelegateMulti ResultDelegate;
	ResultDelegate.Add(InResultDelegate);
	return GetMagicLeapLocationPlugin().GetLastLocationAsync(ResultDelegate, bUseFineLocation);
}

bool UMagicLeapLocationFunctionLibrary::GetLastLocationOnSphere(float InRadius, FVector& OutLocation, bool bUseFineLocation)
{
	return GetMagicLeapLocationPlugin().GetLastLocationOnSphere(InRadius, OutLocation, bUseFineLocation);
}

bool UMagicLeapLocationFunctionLibrary::GetLastLocationOnSphereAsync(const FMagicLeapLocationOnSphereResultDelegate& InResultDelegate, float InRadius, bool bUseFineLocation)
{
	FMagicLeapLocationOnSphereResultDelegateMulti ResultDelegate;
	ResultDelegate.Add(InResultDelegate);
	return GetMagicLeapLocationPlugin().GetLastLocationOnSphereAsync(ResultDelegate, InRadius, bUseFineLocation);
}
