// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "DisplayClusterConfiguratorEditorData.generated.h"

class UDisplayClusterConfigurationData;

UCLASS()
class UDisplayClusterConfiguratorEditorData
	: public UObject
{
	GENERATED_BODY()

public:
	UDisplayClusterConfiguratorEditorData();

public:
	UPROPERTY()
	UDisplayClusterConfigurationData* nDisplayConfig;

	UPROPERTY()
	FString PathToConfig;

public:
	const static TSet<FString> RenderSyncPolicies;

	const static TSet<FString> InputSyncPolicies;

	const static TSet<FString> ProjectionPoliсies;
};
