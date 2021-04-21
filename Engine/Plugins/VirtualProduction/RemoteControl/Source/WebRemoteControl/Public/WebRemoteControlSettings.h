// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WebRemoteControlSettings.generated.h"

UCLASS(config = WebRemoteControl)
class WEBREMOTECONTROL_API UWebRemoteControlSettings : public UObject
{
public:
	GENERATED_BODY()

	UWebRemoteControlSettings()
		: RemoteControlHttpServerPort(30010)
		, RemoteControlWebSocketServerPort(30020)
	{}

public:

	/** Whether web server is started automatically. */
	UPROPERTY(config, EditAnywhere, Category = "Remote Control Web Server")
	bool bAutoStartWebServer = true;
	
	/** Whether web socket server is started automatically. */
	UPROPERTY(config, EditAnywhere, Category = "Remote Control Web Server")
	bool bAutoStartWebSocketServer = true;

	/** The web remote control HTTP server's port. */
	UPROPERTY(config, EditAnywhere, Category = "Remote Control Web Server", DisplayName = "Remote Control HTTP Server Port")
	uint32 RemoteControlHttpServerPort;

	/** The web remote control WebSocket server's port. */
	UPROPERTY(config, EditAnywhere, Category = "Remote Control Web Server", DisplayName = "Remote Control WebSocket Server Port")
	uint32 RemoteControlWebSocketServerPort;
};