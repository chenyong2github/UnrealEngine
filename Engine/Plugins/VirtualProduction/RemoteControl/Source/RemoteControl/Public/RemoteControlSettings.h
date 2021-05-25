// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "RemoteControlSettings.generated.h"


/**
 * Global remote control editor settings
 */
UCLASS(config=Editor)
class REMOTECONTROL_API URemoteControlSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/**
	 * Should generate transaction for protocol events
	 * It makes the transaction for protocls optionals and improve the performance in Editor
	 */
	UPROPERTY(config, EditAnywhere, Category = RemoteControl)
	bool bProtocolsGenerateTransactions = true;
};
