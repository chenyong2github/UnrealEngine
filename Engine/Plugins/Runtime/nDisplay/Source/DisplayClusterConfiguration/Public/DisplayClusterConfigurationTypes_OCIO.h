// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "UObject/ObjectMacros.h"
#include "OpenColorIOColorSpace.h"

#include "DisplayClusterConfigurationTypes_OCIO.generated.h"

/*
 * OCIO profile structure. Can be configured for viewports or cluster nodes.
 * To enable viewport configuration when using as a UPROPERTY set meta = (ConfigurationMode = "Viewports")
 * To enable cluster node configuration when using as a UPROPERTY set meta = (ConfigurationMode = "ClusterNodes")
 */
USTRUCT(Blueprintable)
struct FDisplayClusterConfigurationOCIOConfiguration
{
	GENERATED_BODY()

	FDisplayClusterConfigurationOCIOConfiguration();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OCIO", meta = (DisplayName = "Enable OCIO"))
	bool bIsEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OCIO", meta = (DisplayName = "OCIO Configuration", EditCondition = "bIsEnabled"))
	FOpenColorIODisplayConfiguration OCIOConfiguration;
};

USTRUCT(Blueprintable)
struct FDisplayClusterConfigurationOCIOProfile
{
	GENERATED_BODY()

	FDisplayClusterConfigurationOCIOProfile();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OCIO", meta = (DisplayName = "Enable OCIO"))
	bool bIsEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OCIO", meta = (DisplayName = "OCIO Configuration", EditCondition = "bIsEnabled"))
	FOpenColorIODisplayConfiguration OCIOConfiguration;

	/** The data to receive the profile information. This will either be viewports or nodes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OCIO", meta = (EditCondition = "bIsEnabled"))
	TArray<FString> ApplyOCIOToObjects;
};
