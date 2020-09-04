// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "WebSocketNetworkingDelegates.h"

class WEBSOCKETNETWORKING_API IWebSocketServer
{
public:
	virtual ~IWebSocketServer() {}

	/**
	 * Initialize the server and start listening for messages.
	 * @param Port the port to handle websockets messages on.
	 * @param ClientConnectedCallback the handler called when a client has connected.
	 * @return whether the initialization was successful.
	 */
	virtual bool Init(uint32 Port, FWebSocketClientConnectedCallBack ClientConnectedCallback) = 0;

	/** Tick the server. */
	virtual void Tick() = 0;

	/** Describe this libwebsocket server */
	virtual FString Info() = 0;
};