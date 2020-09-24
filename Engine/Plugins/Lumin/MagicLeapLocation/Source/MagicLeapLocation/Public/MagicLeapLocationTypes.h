// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Engine/Engine.h"
#include "MagicLeapLocationTypes.generated.h"

UENUM(BlueprintType)
enum class EMagicLeapLocationResult : uint8
{
	/** Unknown location error. */
	Unknown,
	/** No connection to server. */
	NoNetworkConnection,
	/** No location data received. */
	NoLocation,
	/** Location provider is not found. */
	ProviderNotFound,
};

/** Location request result. */
USTRUCT(BlueprintType)
struct MAGICLEAPLOCATION_API FMagicLeapLocationData
{
	GENERATED_USTRUCT_BODY()

	/** Location latitude. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Location|MagicLeap")
	float Latitude;

	/** Location longitude. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Location|MagicLeap")
	float Longitude;

	/** Approximate postal code. Remains blank if not provided. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Location|MagicLeap")
	FString PostalCode;

	/** The degree of accuracy in Unreal Units (typically centimeters). Set to -1.0f if not provided. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Location|MagicLeap")
	float Accuracy = -1.0f;
};

/** Delegate used to convey the result of a coarse location query. */
DECLARE_DYNAMIC_DELEGATE_TwoParams(FMagicLeapLocationResultDelegate, const FMagicLeapLocationData&, LocationData, bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMagicLeapLocationResultDelegateMulti, const FMagicLeapLocationData&, LocationData, bool, bSuccess);

/** Delegate used to convey the result of a coarse location on sphere query. */
DECLARE_DYNAMIC_DELEGATE_TwoParams(FMagicLeapLocationOnSphereResultDelegate, const FVector&, LocationOnSphere, bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMagicLeapLocationOnSphereResultDelegateMulti, const FVector&, LocationOnSphere, bool, bSuccess);
