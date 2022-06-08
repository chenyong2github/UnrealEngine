#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "IWebRemoteControlModule.h"
#include "RemoteControlWebsocketRoute.h"

class FStageAppRouteHandler
{
public:
	/** Registers the WebSocket routes with the provided WebRemoteControl module. */
	void RegisterRoutes(IWebRemoteControlModule& WebRemoteControl);

	/** Unregisters the WebSocket routes with the provided WebRemoteControl module. */
	void UnregisterRoutes(IWebRemoteControlModule& WebRemoteControl);

private:
	/** Register a WebSocket route with the WebRemoteControl module. */
	void RegisterRoute(TUniquePtr<FRemoteControlWebsocketRoute> Route);

	/** Handles creating a preview renderer. */
	void HandleWebSocketNDisplayPreviewRendererCreate(const FRemoteControlWebSocketMessage& WebSocketMessage);

	/** Handles setting the root actor of a renderer. */
	void HandleWebSocketNDisplayPreviewRendererSetRoot(const FRemoteControlWebSocketMessage& WebSocketMessage);

	/** Handles destroying a preview renderer. */
	void HandleWebSocketNDisplayPreviewRendererDestroy(const FRemoteControlWebSocketMessage& WebSocketMessage);

	/** Handles rendering a preview. */
	void HandleWebSocketNDisplayPreviewRender(const FRemoteControlWebSocketMessage& WebSocketMessage);

	/** Called when a client disconnects from the WebSocket server. */
	void HandleClientDisconnected(FGuid ClientId);

private:
	/** The module for the WebSocket server for which this is handling routes. */
	IWebRemoteControlModule* RemoteControlModule = nullptr;

	/** Routes this has registered with the WebSocket server. */
	TArray<TUniquePtr<FRemoteControlWebsocketRoute>> Routes;

	/** nDisplay preview renderers belonging to each client. */
	TMap<FGuid, TArray<int32>> NDisplayPreviewRendererIdsByClientId;

	/** Image wrapper module used to compress preview images. */
	class IImageWrapperModule* ImageWrapperModule = nullptr;
};
