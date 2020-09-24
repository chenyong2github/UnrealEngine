// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "Templates/UniquePtr.h"

class IWebSocketServer;

/* 
 * Interface for the WebSocketNetworking module. 
 */
class WEBSOCKETNETWORKING_API IWebSocketNetworkingModule
	: public IModuleInterface
{
public:
	virtual ~IWebSocketNetworkingModule() = default;

	/**
	 * Create a WebSocket server.
	 *
	 * @return A new WebSocket server, or nullptr if the server couldn't be created.
	 */
	virtual TUniquePtr<IWebSocketServer> CreateServer() = 0;
};
