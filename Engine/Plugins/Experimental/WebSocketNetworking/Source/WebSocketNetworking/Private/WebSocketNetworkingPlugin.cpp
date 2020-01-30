// Copyright Epic Games, Inc. All Rights Reserved.

#include "WebSocketNetworkingPrivate.h"


class FWebSocketNetworkingPlugin : public IModuleInterface
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

IMPLEMENT_MODULE(FWebSocketNetworkingPlugin, WebSocketNetworking)

void FWebSocketNetworkingPlugin::StartupModule()
{
}


void FWebSocketNetworkingPlugin::ShutdownModule()
{
}

DEFINE_LOG_CATEGORY(LogWebSocketNetworking);


