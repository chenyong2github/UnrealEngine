// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "DisasterRecoverySettings.generated.h"

UCLASS(config=Engine)
class DISASTERRECOVERYCLIENT_API UDisasterRecoverClientConfig : public UObject
{
	GENERATED_BODY()
public:
	UDisasterRecoverClientConfig();

	/**
	 * True if disaster recovery is enabled.
	 */
	UPROPERTY(config, EditAnywhere, Category="Client Settings")
	bool bIsEnabled = true;
};
