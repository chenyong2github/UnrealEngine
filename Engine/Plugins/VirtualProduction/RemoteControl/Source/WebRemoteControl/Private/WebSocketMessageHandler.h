// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "RemoteControlField.h"


class FRCWebSocketServer;
class FWebRemoteControlModule;
class URemoteControlPreset;
struct FRemoteControlWebSocketMessage;
struct FRemoteControlWebsocketRoute;

/**
  * Class handling web socket message. Registers to required callbacks.
 */
class FWebSocketMessageHandler
{
public:
	FWebSocketMessageHandler(FRCWebSocketServer* InServer);

	/** Register the custom websocket routes with the module. */
	void RegisterRoutes(FWebRemoteControlModule* WebRemoteControl);

	/** Unregister the custom websocket routes from the module */
	void UnregisterRoutes(FWebRemoteControlModule* WebRemoteControl);

private:
	
	/** Handles registration to callbacks to a given preset */
	void HandleWebSocketPresetRegister(const FRemoteControlWebSocketMessage& WebSocketMessage);

	/** Handles unregistration to callbacks to a given preset */
	void HandleWebSocketPresetUnregister(const FRemoteControlWebSocketMessage& WebSocketMessage);

	//Preset callbacks
	void OnPresetExposedPropertyChanged(URemoteControlPreset* Owner, const FRemoteControlProperty& PropertyChanged);
	void OnPropertyExposed(URemoteControlPreset* Owner, FName PropertyLabel);
	void OnPropertyUnexposed(URemoteControlPreset* Owner, FName PropertyLabel);
	void OnFieldRenamed(URemoteControlPreset* Owner, FName OldFieldLabel, FName NewFieldLabel);

	/** Callback when a websocket connection was closed. Let us clean out registrations */
	void OnConnectionClosedCallback(FGuid ClientId);

	/** End of frame callback to send cached property changed, preset changed messages */
	void OnEndFrame();

	/** If properties have changed during the frame, send out notifications to listeners */
	void ProcessChangedProperties();

	/** If new properties were exposed to a preset, send out notifications to listeners */
	void ProcessAddedProperties();

	/** If properties were removed from a preset, send out notifications to listeners */
	void ProcessRemovedProperties();

	/** If fields were renamed, send out notifications to listeners */
	void ProcessRenamedFields();

	/** 
	 * Send a payload to all clients bound to a certain preset.
	 * @note: TargetPresetName must be in the WebSocketNotificationMap.
	 */
	void BroadcastToListeners(FName TargetPresetName, const TArray<uint8>& Payload);

	/**
	 * Returns whether an event targeting a particular preset should be processed.
	 */
	bool ShouldProcessEventForPreset(FName PresetName) const;

private:

	/** Web Socket server. */
	FRCWebSocketServer* Server = nullptr;

	TArray<TUniquePtr<FRemoteControlWebsocketRoute>> Routes;

	/** All websockets connections associated to a preset notifications */
	TMap<FName, TArray<FGuid>> WebSocketNotificationMap;

	/** Properties that changed for a frame, per preset */
	TMap<FName, TArray<FRemoteControlProperty>> PerFramePropertyChanged;

	/** Properties that were exposed for a frame, per preset */
	TMap<FName, TArray<FName>> PerFrameAddedProperties;

	/** Properties that were unexposed for a frame, per preset */
	TMap<FName, TArray<FName>> PerFrameRemovedProperties;

	/** Fields that were renamed for a frame, per preset */
	TMap<FName, TArray<TTuple<FName, FName>>> PerFrameRenamedFields;
};