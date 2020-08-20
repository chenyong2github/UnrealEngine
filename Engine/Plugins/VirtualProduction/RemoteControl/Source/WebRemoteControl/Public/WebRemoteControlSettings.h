// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WebRemoteControlSettings.generated.h"

UCLASS(config = Engine)
class WEBREMOTECONTROL_API UWebRemoteControlSettings : public UObject
{
public:
	GENERATED_BODY()

	UWebRemoteControlSettings()
		: RemoteControlHttpServerPort(8080)
		, RemoteControlWebSocketServerPort(9080)
	{}

	/** The web remote control HTTP server's port. */
	UPROPERTY(config, EditAnywhere, Category = "Web Remote Control", DisplayName = "Remote Control HTTP Server Port")
	uint32 RemoteControlHttpServerPort;

	/** The web remote control WebSocket server's port. */
	UPROPERTY(config, EditAnywhere, Category = "Web Remote Control", DisplayName = "Remote Control WebSocket Server Port")
	uint32 RemoteControlWebSocketServerPort;
};