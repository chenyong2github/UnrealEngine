// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapNetworkingFunctionLibrary.h"
#include "MagicLeapNetworkingPlugin.h"

bool UMagicLeapNetworkingFunctionLibrary::IsInternetConnectedAsync(const FMagicLeapInternetConnectionStatusDelegate& ResultDelegate)
{
	FInternetConnectionStatusDelegateMulti ConnectionQueryResultDelegate;
	ConnectionQueryResultDelegate.Add(ResultDelegate);
	return GetMagicLeapNetworkingPlugin().IsInternetConnectedAsync(ConnectionQueryResultDelegate);
}

bool UMagicLeapNetworkingFunctionLibrary::GetWiFiDataAsync(const FMagicLeapWifiStatusDelegate& ResultDelegate)
{
	FWifiStatusDelegateMulti WifiStatusDelegate;
	WifiStatusDelegate.Add(ResultDelegate);
	return GetMagicLeapNetworkingPlugin().GetWiFiDataAsync(WifiStatusDelegate);
}
