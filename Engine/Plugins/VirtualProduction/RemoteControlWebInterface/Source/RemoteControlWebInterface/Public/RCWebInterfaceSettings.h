// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RCWebInterfaceSettings.generated.h"

UCLASS(config = WebRemoteControl)
class REMOTECONTROLWEBINTERFACE_API URemoteControlWebInterfaceSettings : public UObject
{
public:
	GENERATED_BODY()

public:
	/** The remote control web app http port. */
	UPROPERTY(config, EditAnywhere, Category = "Remote Control Web Interface", DisplayName = "Remote Control Web Interface http Port")
	uint32 RemoteControlWebInterfacePort = 7000;

	/** Should force a build of the WebApp at startup. */
	UPROPERTY(config, EditAnywhere, Category = "Remote Control Web Interface", DisplayName = "Force WebApp build at startup")
	bool bForceWebAppBuildAtStartup = false;

};