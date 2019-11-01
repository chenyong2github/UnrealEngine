// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Engine/Engine.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MagicLeapLocationTypes.h"
#include "MagicLeapLocationFunctionLibrary.generated.h"

UCLASS(ClassGroup = MagicLeap)
class MAGICLEAPLOCATION_API UMagicLeapLocationFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
		Attempts to retrieve the latitude, longitude and postcode of the device.
		@param OutLocation If successful this will contain the latitude, longitude and postcode of the device.
		@return True if the location data is valid, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "Location Function Library | MagicLeap")
	static bool GetLastCoarseLocation(FLocationData& OutLocation);

	/**
		Attempts to retrieve the latitude, longitude and postcode of the device asynchronously.
		@param The delegate to notify once the privilege has been granted.
		@return True if the location is immediately resolved, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "Location | MagicLeap")
	bool GetLastCoarseLocationAsync(const FCoarseLocationResultDelegate& InResultDelegate);

	/**
		Attempts to retrieve a point on a sphere representing the location of the device.
		@param OutLocation If successful this will be a valid point on a sphere representing the location of the device.
		@return True if the location is valid, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "Location Function Library | MagicLeap")
	static bool GetLastCoarseLocationOnSphere(float InRadius, FVector& OutLocation);

	/**
		Attempts to retrieve a point on a sphere representing the location of the device asynchronously.
		@param The delegate to notify once the privilege has been granted.
		@param InRadius The radius of the sphere that the location will be projected onto.
		@return True if the location is immediately resolved, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "Location | MagicLeap")
	bool GetLastCoarseLocationOnSphereAsync(const FCoarseLocationOnSphereResultDelegate& InResultDelegate, float InRadius);
};
