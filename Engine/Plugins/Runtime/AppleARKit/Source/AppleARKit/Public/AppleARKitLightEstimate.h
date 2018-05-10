// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

// UE4
#include "CoreMinimal.h"
#include "Math/SHMath.h"
#include "AppleARKitAvailability.h"

// ARKit
#if SUPPORTS_ARKIT_1_0
	#import <ARKit/ARKit.h>
#endif

// AppleARKit
#include "AppleARKitLightEstimate.generated.h"

/**
 * A light estimate represented as spherical harmonics
 */
USTRUCT( BlueprintType )
struct APPLEARKIT_API FAppleARKitLightEstimate
{
	GENERATED_BODY()

	// Default constructor
	FAppleARKitLightEstimate() {};

#if SUPPORTS_ARKIT_1_0

	/** 
	 * This is a conversion copy-constructor that takes a raw ARLightEstimate and fills this structs 
	 * members with the UE4-ified versions of ARLightEstimate's properties.
	 */ 
	FAppleARKitLightEstimate( ARLightEstimate* InARLightEstimate );

#endif

	/** True if light estimation was enabled for the session and light estimation was successful */
	UPROPERTY( BlueprintReadOnly, Category="AppleARKit|Light Estimate" )
	bool bIsValid;

	/**
	 * Ambient intensity of the lighting.
	 * 
	 * In a well lit environment, this value is close to 1000. It typically ranges from 0 
	 * (very dark) to around 2000 (very bright).
	 */
	UPROPERTY( BlueprintReadOnly, Category="AppleARKit|Light Estimate" )
	float AmbientIntensity;
	
	/**
	 * Color Temperature in Kelvin of light
	 *
	 */
	UPROPERTY( BlueprintReadOnly, Category="AppleARKit|Light Estimate" )
	float AmbientColorTemperatureKelvin;
};
