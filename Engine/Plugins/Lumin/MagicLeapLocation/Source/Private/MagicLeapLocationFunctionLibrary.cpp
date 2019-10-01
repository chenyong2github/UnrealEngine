// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MagicLeapLocationFunctionLibrary.h"
#include "MagicLeapLocationPlugin.h"

bool UMagicLeapLocationFunctionLibrary::GetLastCoarseLocation(FLocationData& OutLocation)
{
	return GET_MAGIC_LEAP_LOCATION_PLUGIN()->GetLastCoarseLocation(OutLocation);
}

bool UMagicLeapLocationFunctionLibrary::GetLastCoarseLocationAsync(const FCoarseLocationResultDelegate& InResultDelegate)
{
	FCoarseLocationResultDelegateMulti ResultDelegate;
	ResultDelegate.Add(InResultDelegate);
	return GET_MAGIC_LEAP_LOCATION_PLUGIN()->GetLastCoarseLocationAsync(ResultDelegate);
}

bool UMagicLeapLocationFunctionLibrary::GetLastCoarseLocationOnSphere(float InRadius, FVector& OutLocation)
{
	return GET_MAGIC_LEAP_LOCATION_PLUGIN()->GetLastCoarseLocationOnSphere(InRadius, OutLocation);
}

bool UMagicLeapLocationFunctionLibrary::GetLastCoarseLocationOnSphereAsync(const FCoarseLocationOnSphereResultDelegate& InResultDelegate, float InRadius)
{
	FCoarseLocationOnSphereResultDelegateMulti ResultDelegate;
	ResultDelegate.Add(InResultDelegate);
	return GET_MAGIC_LEAP_LOCATION_PLUGIN()->GetLastCoarseLocationOnSphereAsync(ResultDelegate, InRadius);
}
