// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "DisplayClusterLightCardEditorHelper.h"
#include "UObject/ObjectMacros.h"
#include "IWebRemoteControlModule.h"
#include "RemoteControlWebsocketRoute.h"
#include "StageAppRequest.h"

class ADisplayClusterRootActor;
class ADisplayClusterLightCardActor;

/** Handler for Epic Stage App websocket requests. */
class FStageAppRouteHandler
{
private:
	/** Data used for editing lightcard positions within the context of a preview. */
	struct FPerRendererData
	{
		FPerRendererData(int RendererId);

		/** The list of light cards currently being dragged by this client. Empty if a drag is not in progress. */
		TArray<TWeakObjectPtr<ADisplayClusterLightCardActor>> DraggedLightCards;

		/** The light card to use as the origin point for the drag operation. */
		TWeakObjectPtr<ADisplayClusterLightCardActor> PrimaryLightCard;

		/** The last position sent by a client during a drag operation. */
		FVector2D LastDragPosition;

		/** If true, the dragged lightcards have moved since the last timeout check. */
		bool bHasDragMovedRecently;

	public:
		/** Get or create a FDisplayClusterLightCardEditorHelper for this client preview renderer. */
		FDisplayClusterLightCardEditorHelper& GetLightCardHelper();

		/** Get the preview settings to use for renders. */
		const FRCWebSocketNDisplayPreviewRendererSettings& GetPreviewSettings() const;

		/** Change the preview settings to use for renders. */
		void SetPreviewSettings(const FRCWebSocketNDisplayPreviewRendererSettings& NewSettings);

		/** Get the scene view init options to be used for preview renders based on the stored settings. */
		bool GetSceneViewInitOptions(FSceneViewInitOptions& ViewInitOptions);

		/** Update the sequence number if the provided one is higher than the previous. */
		void UpdateDragSequenceNumber(int64 ReceivedSequenceNumber);

		/** Get the highest sequence number received from the client during a drag operation. */
		int64 GetDragSequenceNumber() const;

		/** Get the root actor used for previews. */
		ADisplayClusterRootActor* GetRootActor() const;

	private:
		/** Update LightCardHelper's settings based on the ones stored in PreviewSettings. */
		void UpdateLightCardHelperSettings();

	private:
		/** Helper used to move light cards and project their coordinates. */
		TSharedPtr<FDisplayClusterLightCardEditorHelper> LightCardHelper;

		/** Settings used for generating previews. */
		FRCWebSocketNDisplayPreviewRendererSettings PreviewSettings;

		/** The ID of the renderer with which this is associated. */
		int RendererId;

		/** Highest sequence number received from the client during a drag operation. */
		int64 SequenceNumber = 0;
	};

	/** Map from renderer IDs created by a client (keyed by ID) to per-renderer data used by the handler. */
	using TClientIdToPerRendererDataMap = TMap<int32, FPerRendererData>;

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

	/** Handles changing renderer settings. */
	void HandleWebSocketNDisplayPreviewRendererConfigure(const FRemoteControlWebSocketMessage& WebSocketMessage);

	/** Handles destroying a preview renderer. */
	void HandleWebSocketNDisplayPreviewRendererDestroy(const FRemoteControlWebSocketMessage& WebSocketMessage);

	/** Handles rendering a preview. */
	void HandleWebSocketNDisplayPreviewRender(const FRemoteControlWebSocketMessage& WebSocketMessage);

	/** Handles beginning a light card drag operation. */
	void HandleWebSocketNDisplayPreviewLightCardDragBegin(const FRemoteControlWebSocketMessage& WebSocketMessage);

	/** Handles moving light cards with a drag operation. */
	void HandleWebSocketNDisplayPreviewLightCardDragMove(const FRemoteControlWebSocketMessage& WebSocketMessage);

	/** Handles ending a light card drag operation. */
	void HandleWebSocketNDisplayPreviewLightCardDragEnd(const FRemoteControlWebSocketMessage& WebSocketMessage);

	/** Called when a client disconnects from the WebSocket server. */
	void HandleClientDisconnected(FGuid ClientId);

	/** Change the settings for a client's preview renderer. */
	void ChangePreviewRendererSettings(const FGuid& ClientId, int32 RendererId, const FRCWebSocketNDisplayPreviewRendererSettings& NewSettings);

	/** Get the nDisplay preview rendering data for the given client and renderer ID, or null if it's an invalid client/renderer ID. */
	FPerRendererData* GetClientPerRendererData(const FGuid& ClientId, int32 RendererId);

	/** Drag light cards to the specified position. */
	void DragLightCards(FPerRendererData& PreviewData, FVector2D DragPosition);

	/** Check if any light card drag operations have timed out, and if so, end them. */
	bool TimeOutDrags(float DeltaTime);

	/** End a client's light card drag operation. */
	void EndLightCardDrag(FPerRendererData& PreviewData, const FGuid& ClientId, int32 RendererId, bool bEndedByClient);

private:
	/** The module for the WebSocket server for which this is handling routes. */
	IWebRemoteControlModule* RemoteControlModule = nullptr;

	/** Routes this has registered with the WebSocket server. */
	TArray<TUniquePtr<FRemoteControlWebsocketRoute>> Routes;

	/** Map from client GUID to preview renderer data maps for that client. */
	TMap<FGuid, TClientIdToPerRendererDataMap> PerRendererDataMapsByClientId;

	/** Image wrapper module used to compress preview images. */
	class IImageWrapperModule* ImageWrapperModule = nullptr;

	/** Handle for timing out drags. */
	FTSTicker::FDelegateHandle DragTimeoutTickerHandle;
};
