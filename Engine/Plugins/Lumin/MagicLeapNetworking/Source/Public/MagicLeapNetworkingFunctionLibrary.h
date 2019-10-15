// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/Engine.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MagicLeapNetworkingTypes.h"
#include "MagicLeapNetworkingFunctionLibrary.generated.h"

UCLASS(ClassGroup = MagicLeap)
class MAGICLEAPNETWORKING_API UMagicLeapNetworkingFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
		Asynchronously queries whether or not the internet is connected.  If the delegate broadcasts that the connection is active,
		it means link layer is up, internet is accessible, and DNS is good.
		@param ResultDelegate The delegate that will convey the result of the connection query.
		@return True if the async task is successfully created, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "Networking Function Library | MagicLeap")
	static bool IsInternetConnectedAsync(const FMagicLeapInternetConnectionStatusDelegate& ResultDelegate);

	/** 
		Asynchronously queries the device's WiFi related data.
		@param ResultDelegate The delegate that will convey the wifi data.
		@return True if the async task is successfully created, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "Networking Function Library | MagicLeap")
	static bool GetWiFiDataAsync(const FMagicLeapWifiStatusDelegate& ResultDelegate);
};
