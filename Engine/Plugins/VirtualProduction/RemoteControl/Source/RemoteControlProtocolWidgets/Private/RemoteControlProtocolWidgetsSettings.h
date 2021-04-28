// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "RemoteControlProtocolWidgetsSettings.generated.h"

/**
 * Remote Control Protocol Widget Settings
 */
UCLASS(Config = Engine, DefaultConfig)
class REMOTECONTROLPROTOCOLWIDGETS_API URemoteControlProtocolWidgetsSettings : public UObject
{
	GENERATED_BODY()

public:
	/** Hidden protocol type name */
	UPROPERTY(Config, EditAnywhere, Category = Widgets)
	TSet<FName> HiddenProtocolTypeNames;
};
