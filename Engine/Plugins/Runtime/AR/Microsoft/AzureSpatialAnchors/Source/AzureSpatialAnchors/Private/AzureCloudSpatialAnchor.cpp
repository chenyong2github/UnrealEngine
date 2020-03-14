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

void UAzureCloudSpatialAnchor::SetExpiration(float Lifetime)
{
	IAzureSpatialAnchors* MSA = IAzureSpatialAnchors::Get();
	if (MSA == nullptr)
	{
		return;
	}

	MSA->SetCloudAnchorExpiration(this, Lifetime);
}

float UAzureCloudSpatialAnchor::GetExpiration() const
{
	IAzureSpatialAnchors* MSA = IAzureSpatialAnchors::Get();
	if (MSA == nullptr)
	{
		return 0.0f;
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