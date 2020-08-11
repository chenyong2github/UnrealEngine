// Copyright Epic Games, Inc. All Rights Reserved.

#include "IWebSocketNetworkingModule.h"
#include "WebSocketNetworkingPrivate.h"
#include "WebSocketServer.h"


class FWebSocketNetworkingPlugin : public IWebSocketNetworkingModule
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override
	{
	}

	virtual void ShutdownModule() override
	{
	}


public:
	TUniquePtr<IWebSocketServer> CreateServer() override
	{
		return MakeUnique<FWebSocketServer>();
	}

};

IMPLEMENT_MODULE(FWebSocketNetworkingPlugin, WebSocketNetworking)

DEFINE_LOG_CATEGORY(LogWebSocketNetworking);


