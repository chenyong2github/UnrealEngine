// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/Engine.h"
#include "Components/ActorComponent.h"
#include "MagicLeapLocationTypes.h"
#include "MagicLeapLocationComponent.generated.h"

/**
	Component that provides access to the Location API functionality.
*/
UCLASS(ClassGroup = MagicLeap, BlueprintType, Blueprintable, EditInlineNew, meta = (BlueprintSpawnableComponent))
class MAGICLEAPLOCATION_API UMagicLeapLocationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/**
		Attempts to retrieve the latitude, longitude and postcode of the device.
		@param OutLocation If successful this will contain the latitude, longitude and postcode of the device.
		@param bUseFineLocation Flags whether or not to request a fine or coarse location.
		@return True if the location data is valid, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "Location | MagicLeap")
	bool GetLastLocation(FMagicLeapLocationData& OutLocation, bool bUseFineLocation = true);

	/**
		Attempts to retrieve the latitude, longitude and postcode of the device asynchronously.
		@param bUseFineLocation Flags whether or not to request a fine or coarse location.
		@return True if the location is immediately resolved, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "Location | MagicLeap")
	bool GetLastLocationAsync(bool bUseFineLocation = true);

	/**
		Attempts to retrieve a point on a sphere representing the location of the device.
		@param InRadius The radius of the sphere that the location will be projected onto.
		@param OutLocation If successful this will be a valid point on a sphere representing the location of the device.
		@param bUseFineLocation Flags whether or not to request a fine or coarse location.
		@return True if the location is valid, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "Location | MagicLeap")
	bool GetLastLocationOnSphere(float InRadius, FVector& OutLocation, bool bUseFineLocation = true);

	/**
		Attempts to retrieve a point on a sphere representing the location of the device asynchronously.
		@param InRadius The radius of the sphere that the location will be projected onto.
		@param bUseFineLocation Flags whether or not to request a fine or coarse location.
		@return True if the location is immediately resolved, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "Location | MagicLeap")
	bool GetLastLocationOnSphereAsync(float InRadius, bool bUseFineLocation = true);

private:
	// Delegate instances
	UPROPERTY(BlueprintAssignable, Category = "Location | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapLocationResultDelegateMulti OnGotLocation;

	UPROPERTY(BlueprintAssignable, Category = "Location | MagicLeap", meta = (AllowPrivateAccess = true))
	FMagicLeapLocationOnSphereResultDelegateMulti OnGotLocationOnSphere;
};
