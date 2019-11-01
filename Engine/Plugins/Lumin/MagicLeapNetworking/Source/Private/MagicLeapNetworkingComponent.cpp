// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MagicLeapNetworkingComponent.h"
#include "MagicLeapNetworkingPlugin.h"

bool UMagicLeapNetworkingComponent::IsInternetConnectedAsync()
{
	return GetMagicLeapNetworkingPlugin().IsInternetConnectedAsync(ConnectionQueryResultDeleage);
}

bool UMagicLeapNetworkingComponent::GetWiFiDataAsync()
{
	return GetMagicLeapNetworkingPlugin().GetWiFiDataAsync(WifiDataQueryResultDelegate);
}
