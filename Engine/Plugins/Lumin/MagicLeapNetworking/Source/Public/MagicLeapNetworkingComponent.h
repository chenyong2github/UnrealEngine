// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/Engine.h"
#include "Components/ActorComponent.h"
#include "MagicLeapNetworkingTypes.h"
#include "MagicLeapNetworkingComponent.generated.h"

/**
	Component that provides access to the Networking API functionality.
*/
UCLASS(ClassGroup = MagicLeap, BlueprintType, Blueprintable, EditInlineNew, meta = (BlueprintSpawnableComponent))
class MAGICLEAPNETWORKING_API UMagicLeapNetworkingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/**
		Asynchronously queries whether or not the internet is connected.  If the delegate broadcasts that the connection is active,
		it means link layer is up, internet is accessible, and DNS is good.
		@param ResultDelegate The delegate that will convey the result of the connection query.
		@return True if the async task is successfully created, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "Networking | MagicLeap")
	bool IsInternetConnectedAsync();

	/**
		Asynchronously queries the device's WiFi related data.
		@param ResultDelegate The delegate that will convey the wifi data.
		@return True if the async task is successfully created, false otherwise.
	*/
	UFUNCTION(BlueprintCallable, Category = "Networking | MagicLeap")
	bool GetWiFiDataAsync();

private:
	UPROPERTY(BlueprintAssignable, Category = "Networking | MagicLeap", meta = (AllowPrivateAccess = true))
	FInternetConnectionStatusDelegateMulti ConnectionQueryResultDeleage;

	UPROPERTY(BlueprintAssignable, Category = "Networking | MagicLeap", meta = (AllowPrivateAccess = true))
	FWifiStatusDelegateMulti WifiDataQueryResultDelegate;
};
