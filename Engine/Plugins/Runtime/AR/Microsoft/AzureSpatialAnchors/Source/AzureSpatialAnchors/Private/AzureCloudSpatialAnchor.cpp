// Copyright Epic Games, Inc. All Rights Reserved.

#include "AzureCloudSpatialAnchor.h"

#include "Engine/Engine.h"
#include "IAzureSpatialAnchors.h"

FString UAzureCloudSpatialAnchor::GetAzureCloudIdentifier() const
{
	IAzureSpatialAnchors* MSA = IAzureSpatialAnchors::Get();
	if (MSA == nullptr)
	{
		return FString();
	}

	return MSA->GetCloudSpatialAnchorIdentifier(CloudAnchorID);
}

void UAzureCloudSpatialAnchor::SetExpiration(const FDateTime InExpiration)
{
	IAzureSpatialAnchors* MSA = IAzureSpatialAnchors::Get();
	if (MSA == nullptr)
	{
		return;
	}

	MSA->SetCloudAnchorExpiration(this, InExpiration);
}

FDateTime UAzureCloudSpatialAnchor::GetExpiration() const
{
	IAzureSpatialAnchors* MSA = IAzureSpatialAnchors::Get();
	if (MSA == nullptr)
	{
		return FDateTime();
	}

	return MSA->GetCloudAnchorExpiration(this);
}

void UAzureCloudSpatialAnchor::SetAppProperties(const TMap<FString, FString>& InAppProperties)
{
	IAzureSpatialAnchors* MSA = IAzureSpatialAnchors::Get();
	if (MSA == nullptr)
	{
		return;
	}

	MSA->SetCloudAnchorAppProperties(this, InAppProperties);
}


TMap<FString, FString> UAzureCloudSpatialAnchor::GetAppProperties() const
{
	IAzureSpatialAnchors* MSA = IAzureSpatialAnchors::Get();
	if (MSA == nullptr)
	{
		return TMap<FString, FString>();
	}

	return MSA->GetCloudAnchorAppProperties(this);
}