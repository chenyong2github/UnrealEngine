// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Engine/Engine.h"
#include "MagicLeapLocationTypes.generated.h"

UENUM(BlueprintType)
enum class EMagicLeapLocationResult : uint8
{
	/** Unknown location error. */
	Unknown,
	/** No connection to server. */
	NetworkConnection,
	/** No location data received. */
	NoLocation,
	/** Location provider is not found. */
	ProviderNotFound,
};

/** Location request result. */
USTRUCT(BlueprintType)
struct FLocationData
{
	GENERATED_USTRUCT_BODY()

	/** Location latitude. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Location|MagicLeap")
	float Latitude;

	/** Location longitude. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Location|MagicLeap")
	float Longitude;

	/** Location longitude. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Location|MagicLeap")
	FString PostalCode;
};

/** Delegate used to convey the result of a coarse location query. */
DECLARE_DYNAMIC_DELEGATE_TwoParams(FCoarseLocationResultDelegate, const FLocationData&, LocationData, bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCoarseLocationResultDelegateMulti, const FLocationData&, LocationData, bool, bSuccess);

/** Delegate used to convey the result of a coarse location on sphere query. */
DECLARE_DYNAMIC_DELEGATE_TwoParams(FCoarseLocationOnSphereResultDelegate, const FVector&, LocationOnSphere, bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCoarseLocationOnSphereResultDelegateMulti, const FVector&, LocationOnSphere, bool, bSuccess);
