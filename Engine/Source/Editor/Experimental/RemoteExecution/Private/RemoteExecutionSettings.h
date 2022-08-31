// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "RemoteExecutionSettings"
#include "UObject/Object.h"
#include "UObject/UObjectGlobals.h"

UCLASS(config = EditorSettings)
class URemoteExecutionSettings : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	/** The remote executor we prefer to use. */
	UPROPERTY(Config, EditAnywhere, Category = "Remote Execution", meta = (DisplayName = "Preferred Executor", ConfigRestartRequired = true))
	FString PreferredRemoteExecutor;
};
