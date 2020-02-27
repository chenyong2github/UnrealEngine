// Copyright Epic Games, Inc. All Rights Reserved.

#include "AzureCloudSpatialAnchor.h"

#include "Engine/Engine.h"

TMap<FString, FString> UAzureCloudSpatialAnchor::GetAppProperties() const
{
	return TMap<FString, FString>();
}

FDateTime UAzureCloudSpatialAnchor::GetExpiration() const
{
	return FDateTime();
}

FString UAzureCloudSpatialAnchor::GetIdentifier() const
{
	return FString();
}
