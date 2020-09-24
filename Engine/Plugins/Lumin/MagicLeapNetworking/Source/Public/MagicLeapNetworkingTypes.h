// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/Engine.h"
#include "MagicLeapNetworkingTypes.generated.h"

/** WiFi related data. */
USTRUCT(BlueprintType)
struct MAGICLEAPNETWORKING_API FMagicLeapNetworkingWiFiData
{
	GENERATED_USTRUCT_BODY()

	/** WiFi RSSI in dbM. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Networking|MagicLeap")
	int32 RSSI;

	/** WiFi link speed in Mb/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Networking|MagicLeap")
	int32 Linkspeed;

	/** WiFi frequency in MHz. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Networking|MagicLeap")
	float Frequency;
};

/** Delegate used to convey the result of an internet connection query. */
DECLARE_DYNAMIC_DELEGATE_OneParam(FMagicLeapInternetConnectionStatusDelegate, bool, bIsConnected);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FInternetConnectionStatusDelegateMulti, bool, bIsConnected);

/** Delegate used to convey the result of wifi data query. */
DECLARE_DYNAMIC_DELEGATE_OneParam(FMagicLeapWifiStatusDelegate, FMagicLeapNetworkingWiFiData, WiFiData);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWifiStatusDelegateMulti, FMagicLeapNetworkingWiFiData, WiFiData);
