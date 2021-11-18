// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "RemoteControlProtocolDMX.h"
#include "UObject/Object.h"

#include "RemoteControlProtocolDMXSettings.generated.h"

/**
 * DMX Remote Control Settings
 */
UCLASS(Config = Engine, DefaultConfig)
class REMOTECONTROLPROTOCOLDMX_API URemoteControlProtocolDMXSettings : public UObject
{
	GENERATED_BODY()

public:
	//~ Begin UObject interface
	virtual void PostInitProperties() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End UObject interface

	/** DMX Default Device */
	UPROPERTY(Config, EditAnywhere, Category = Mapping)
	FGuid DefaultInputPortId;

	/** Returns a delegate broadcast whenever the Remote Control Protocol DMX Settings changed */
	static FSimpleMulticastDelegate& GetOnRemoteControlProtocolDMXSettingsChanged();

private:
	static FSimpleMulticastDelegate OnRemoteControlProtocolDMXSettingsChangedDelegate;
};
