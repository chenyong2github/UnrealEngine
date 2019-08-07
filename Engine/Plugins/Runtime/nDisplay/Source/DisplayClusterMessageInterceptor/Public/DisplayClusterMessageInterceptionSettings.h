// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "DisplayClusterMessageInterceptionSettings.generated.h"

UCLASS(config=Engine)
class DISPLAYCLUSTERMESSAGEINTERCEPTION_API UDisplayClusterMessageInterceptionSettings : public UObject
{
	GENERATED_BODY()

public:
	/** Indicates if message interception is enabled. */
	UPROPERTY(config, EditAnywhere, Category = "Interception Settings")
	bool bIsEnabled = false;

	/** List of message types that should be taken into consideration for interception. */
	UPROPERTY(config, EditAnywhere, Category = "Interception Settings")
	TArray<FName> MessageTypes;

	/** The annotations to look for identifying messages across the cluster. */
	UPROPERTY(config, EditAnywhere, Category = "Interception Settings")
	FName Annotation;
};