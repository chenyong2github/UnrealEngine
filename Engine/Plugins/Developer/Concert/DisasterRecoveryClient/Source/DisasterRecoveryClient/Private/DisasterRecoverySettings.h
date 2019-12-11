// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/EngineTypes.h"
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

	/**
	 * The root directory where recovery sessions should be stored. If not set or
	 * invalid, the recovery sessions are stored in the project saved directory.
	 */
	UPROPERTY(config) // TODO: Expose it once properly tested.
	FDirectoryPath RecoverySessionDir;

	/**
	 * The number of recovery session to keep around in the history.
	 */
	UPROPERTY(config) // TODO: Expose it when the user will be able to pick up a session from a list. No need to keep sessions around until the user cannot view/select one.
	int32 SessionHistorySize = 0;
};
