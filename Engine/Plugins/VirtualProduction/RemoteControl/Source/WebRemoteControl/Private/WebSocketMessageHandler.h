// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "RemoteControlActor.h"
#include "RemoteControlField.h"
#include "RemoteControlPreset.h"


struct FGuid;
class FRCWebSocketServer;
struct FRemoteControlActor;
struct FRemoteControlWebSocketMessage;
struct FRemoteControlWebsocketRoute;
struct FRCObjectReference;
class FWebRemoteControlModule;

/**
  * Class handling web socket message. Registers to required callbacks.
 */
class FWebSocketMessageHandler
{
public: 
	FWebSocketMessageHandler(FRCWebSocketServer* InServer, const FGuid& InActingClientId);

	/** Register the custom websocket routes with the module. */
	void RegisterRoutes(FWebRemoteControlModule* WebRemoteControl);

	/** Unregister the custom websocket routes from the module */
	void UnregisterRoutes(FWebRemoteControlModule* WebRemoteControl);

	/** Notify that a property was modified by a web client. */
	void NotifyPropertyChangedRemotely(const FGuid& OriginClientId, const FGuid& PresetId, const FGuid& ExposedPropertyId);

private:
	
	/** Handles registration to callbacks to a given preset */
	void HandleWebSocketPresetRegister(const FRemoteControlWebSocketMessage& WebSocketMessage);

	/** Handles unregistration to callbacks to a given preset */
	void HandleWebSocketPresetUnregister(const FRemoteControlWebSocketMessage& WebSocketMessage);

	//Preset callbacks
	void OnPresetExposedPropertiesModified(URemoteControlPreset* Owner, const TSet<FGuid>& ModifiedPropertyIds);
	void OnPropertyExposed(URemoteControlPreset* Owner,  const FGuid& EntityId);
	void OnPropertyUnexposed(URemoteControlPreset* Owner, const FGuid& EntityId);
	void OnFieldRenamed(URemoteControlPreset* Owner, FName OldFieldLabel, FName NewFieldLabel);
	void OnMetadataModified(URemoteControlPreset* Owner);
	void OnActorPropertyChanged(URemoteControlPreset* Owner, FRemoteControlActor& Actor, UObject* ModifiedObject, FProperty* ModifiedProperty);
	void OnEntitiesModified(URemoteControlPreset* Owner, const TSet<FGuid>& ModifiedEntities);
	void OnLayoutModified(URemoteControlPreset* Owner);

	/** Callback when a websocket connection was closed. Let us clean out registrations */
	void OnConnectionClosedCallback(FGuid ClientId);

	/** End of frame callback to send cached property changed, preset changed messages */
	void OnEndFrame();

	/** If properties have changed during the frame, send out notifications to listeners */
	void ProcessChangedProperties();

	/** If an exposed actor's properties have changed during the frame, send out notifications to listeners */
	void ProcessChangedActorProperties();

	/** If new properties were exposed to a preset, send out notifications to listeners */
	void ProcessAddedProperties();

	/** If properties were removed from a preset, send out notifications to listeners */
	void ProcessRemovedProperties();

	/** If fields were renamed, send out notifications to listeners */
	void ProcessRenamedFields();

	/** If metadata was modified on a preset, notify listeners. */
	void ProcessModifiedMetadata();

	/** If a preset layout is modified, notify listeners. */
	void ProcessModifiedPresetLayouts();

	/** 
	 * Send a payload to all clients bound to a certain preset.
	 * @note: TargetPresetName must be in the WebSocketNotificationMap.
	 */
	void BroadcastToListeners(const FGuid& TargetPresetId, const TArray<uint8>& Payload);

	/**
	 * Returns whether an event targeting a particular preset should be processed.
	 */
	bool ShouldProcessEventForPreset(const FGuid& PresetId) const;

	/**
	 * Write the provided list of events to a buffer.
	 */
	bool WritePropertyChangeEventPayload(URemoteControlPreset* InPreset, const TSet<FGuid>& InModifiedPropertyIds, TArray<uint8>& OutBuffer);

	/**
	 * Write the provided list of actor modifications to a buffer.
	 */
	bool WriteActorPropertyChangePayload(URemoteControlPreset* InPreset, const TMap<FRemoteControlActor, TArray<FRCObjectReference>>& InModifications, TArray<uint8>& OutBuffer);

private:

	/** Web Socket server. */
	FRCWebSocketServer* Server = nullptr;

	TArray<TUniquePtr<FRemoteControlWebsocketRoute>> Routes;

	/** All websockets connections associated to a preset notifications */
	TMap<FGuid, TArray<FGuid>> WebSocketNotificationMap;

	/** Configuration for a given client related to how events should be handled. */
	struct FRCClientConfig
	{
		/** Whether the client ignores events that were initiated remotely. */
		bool bIgnoreRemoteChanges = false;	
	};

	/** Holds client-specific config if any. */
	TMap<FGuid, FRCClientConfig> ClientConfigMap;

	/** Properties that changed for a frame, per preset.  */
	TMap<FGuid, TMap<FGuid, TSet<FGuid>>> PerFrameModifiedProperties;

	/** 
	 * List of properties modified remotely this frame, used to not trigger a 
	 * change notification after a post edit change for a property that was modified remotely.
	 */
	TSet<FGuid> PropertiesManuallyNotifiedThisFrame;

	/** Properties that changed on an exposed actor for a given client, for a frame, per preset.  */
	TMap<FGuid, TMap<FGuid, TMap<FRemoteControlActor, TArray<FRCObjectReference>>>> PerFrameActorPropertyChanged;

	/** Properties that were exposed for a frame, per preset */
	TMap<FGuid, TArray<FGuid>> PerFrameAddedProperties;

	/** Properties that were unexposed for a frame, per preset */
	TMap<FGuid, TTuple<TArray<FGuid>, TArray<FName>>> PerFrameRemovedProperties;

	/** Fields that were renamed for a frame, per preset */
	TMap<FGuid, TArray<TTuple<FName, FName>>> PerFrameRenamedFields;

	/** Presets that had their metadata modified for a frame */
	TSet<FGuid> PerFrameModifiedMetadata;

	/** Presets that had their layout modified for a frame. */
	TSet<FGuid> PerFrameModifiedPresetLayouts;
	
	/** Holds the ID of the client currently making a request. Used to prevent sending back notifications to it. */
	const FGuid& ActingClientId;
	
	/** Frame counter for delaying property change checks. */
	int32 PropertyNotificationFrameCounter = 0;
};