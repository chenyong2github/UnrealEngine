// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "IRemoteControlModule.h"
#include "RemoteControlRequest.h"
#include "HttpRouteHandle.h"
#include "HttpServerResponse.h"
#include "HttpServerRequest.h"
#include "RemoteControlRoute.h"
#include "HAL/IConsoleManager.h"
#include "WebRemoteControlEditorRoutes.h"
#include "RemoteControlWebSocketServer.h"

struct FHttpServerRequest;
class IHttpRouter;

/**
 * A Remote Control module that expose remote function calls through http
 */
class FWebRemoteControlModule : public IModuleInterface
{
public:
	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static FWebRemoteControlModule& Get()
	{
		static const FName ModuleName = "WebRemoteControl";
		return FModuleManager::LoadModuleChecked<FWebRemoteControlModule>(ModuleName);
	}

	//~ IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/**
	 * Register a route to the API.
	 * @param Route The route to register.
	 */
	void RegisterRoute(const FRemoteControlRoute& Route);

	/**
	 * Unregister a route to the API.
	 * @param Route The route to unregister.
	 */
	void UnregisterRoute(const FRemoteControlRoute& Route);

	/**
	 * Register a websocket route.
	 * @param Route the route to register.
	 */
	void RegisterWebsocketRoute(const FRemoteControlWebsocketRoute& Route);

	/**
	 * Unregister a websocket route.
	 * @param Route the route to unregister.
	 */
	void UnregisterWebsocketRoute(const FRemoteControlWebsocketRoute& Route);

	/**
	 * Start the web control server
	 */
	void StartHttpServer();

	/**
	 * Stop the web control server.
	 */
	void StopHttpServer();

	/**
	 * Start the web control websocket server.
	 */
	void StartWebSocketServer();

	/**
	 * Stop the web control websocket server.
	 */
	void StopWebSocketServer();

private:
	/** Bind the route in the http router and add it to the list of active routes. */
	void StartRoute(const FRemoteControlRoute& Route);

	/** Register HTTP and Websocket routes. */
	void RegisterRoutes();

	/** Register console commands. */
	void RegisterConsoleCommands();

	/** Unregister console commands. */
	void UnregisterConsoleCommands();

	//~ Route handlers
	bool HandleInfoRoute(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleOptionsRoute(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleObjectCallRoute(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleObjectPropertyRoute(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

#if WITH_EDITOR
	//~ Settings
	void RegisterSettings();
	void UnregisterSettings();
	bool OnSettingsModified();
#endif

	/** Console commands handles. */
	TArray<TUniquePtr<FAutoConsoleCommand>> ConsoleCommands;

	/** Http router handle */
	TSharedPtr<IHttpRouter> HttpRouter;

	/** Mapping of routes to delegate handles */
	TMap<uint32, FHttpRouteHandle> ActiveRouteHandles;

	/** Set of routes that will be activated on http server start. */
	TSet<FRemoteControlRoute> RegisteredHttpRoutes;

	/** Set of routes that will be activated on websocket server start. */
	TSet<FRemoteControlRoute> RegisteredWebSocketRoutes;

	/** Port of the remote control http server. */
	uint32 HttpServerPort;

	/** Port of the remote control websocket server. */
	uint32 WebSocketServerPort;

	/** Routes that are editor specific. */
	FWebRemoteControlEditorRoutes EditorRoutes;
	
	/** Server that serves websocket requests. */
	FRCWebSocketServer WebSocketServer;

	/** Router used to dispatch websocket messages. */
	TSharedPtr<FWebsocketMessageRouter> WebSocketRouter;
};