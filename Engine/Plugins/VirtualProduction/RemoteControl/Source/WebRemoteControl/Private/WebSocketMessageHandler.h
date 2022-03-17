// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "RemoteControlActor.h"
#include "RemoteControlField.h"
#include "RemoteControlModels.h"
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

	/** Data about a watched actor so we know who to notify and what to send if the actor is garbage-collected before we know it's been deleted. */
	struct FWatchedActorData
	{
		FWatchedActorData(AActor* InActor)
			: Description(InActor)
		{
		}

		/** Description of the actor. */
		FRCActorDescription Description;

		/** Which clients are listening for events about this actor. */
		TArray<FGuid> Clients;
	};

	/** Register handlers for actors being added/deleted (must happen after engine init). */
	void RegisterActorHandlers();
	
	/** Handles registration to callbacks to a given preset */
	void HandleWebSocketPresetRegister(const FRemoteControlWebSocketMessage& WebSocketMessage);

	/** Handles unregistration to callbacks to a given preset */
	void HandleWebSocketPresetUnregister(const FRemoteControlWebSocketMessage& WebSocketMessage);

	/** Handles registration to callbacks for creation/destruction/rename of a given actor type */
	void HandleWebSocketActorRegister(const FRemoteControlWebSocketMessage& WebSocketMessage);

	/** Handles registration to callbacks for creation/destruction/rename of a given actor type */
	void HandleWebSocketActorUnregister(const FRemoteControlWebSocketMessage& WebSocketMessage);

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

	/** If actors were added/removed/renamed, notify listeners. */
	void ProcessActorChanges();

	/** 
	 * Send a payload to all clients bound to a certain preset.
	 * @note: TargetPresetName must be in the PresetNotificationMap.
	 */
	void BroadcastToPresetListeners(const FGuid& TargetPresetId, const TArray<uint8>& Payload);

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
	bool WriteActorPropertyChangePayload(URemoteControlPreset* InPreset, const TMap<FRemoteControlActor, TArray<FRCObjectReference>>& InModifications, FMemoryWriter& InWriter);

	/**
	 * Called when an actor is added to the current world.
	 */
	void OnActorAdded(AActor* Actor);

	/**
	 * Called when an actor is deleted from the current world.
	 */
	void OnActorDeleted(AActor* Actor);

	/**
	 * Called when an untracked change to the actor list happens in the editor.
	 */
	void OnActorListChanged();

	/**
	 * Called when an object's property is changed in the editor. Used to detect name changes on subscribed actors.
	 */
	void OnObjectPropertyChanged(UObject* Object, struct FPropertyChangedEvent& Event);

	/**
	 * Called when an object is affected by an editor transaction. Used to detect undo/redo of creating/deleting subscribed actors.
	 */
	void OnObjectTransacted(UObject* Object, const class FTransactionObjectEvent& TransactionEvent);

	/**
	 * Start watching an actor for the given client.
	 */
	void StartWatchingActor(AActor* Actor, FGuid ClientId);

	/**
	 * Stop watching an actor for the given client.
	 */
	void StopWatchingActor(AActor* Actor, FGuid ClientId);

	/**
	 * Update our cache of watched actor's name and notify any subscribers.
	 */
	void UpdateWatchedActorName(AActor* Actor, FWatchedActorData& ActorData);

private:

	/** Web Socket server. */
	FRCWebSocketServer* Server = nullptr;

	TArray<TUniquePtr<FRemoteControlWebsocketRoute>> Routes;

	/** All websockets connections associated to a preset notifications */
	TMap<FGuid, TArray<FGuid>> PresetNotificationMap;

	/** All websocket client IDs associated with an actor class. */
	typedef TMap<TWeakObjectPtr<UClass>, TArray<FGuid>, FDefaultSetAllocator, TWeakObjectPtrMapKeyFuncs<TWeakObjectPtr<UClass>, TArray<FGuid>>> FActorNotificationMap;
	FActorNotificationMap ActorNotificationMap;

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

	/** Actors that were added for a frame, per subscribed client. */
	TMap<FGuid, TArray<TWeakObjectPtr<AActor>>> PerFrameActorsAdded;

	/** Actors that were renamed for a frame, per subscribed client. */
	TMap<FGuid, TArray<TWeakObjectPtr<AActor>>> PerFrameActorsRenamed;

	/**
	 * Actors that were removed for a frame, per subscribed client.
	 * Stored by description in case the actor is garbage collected before its name and path can be collected.
	 */
	TMap<FGuid, TArray<FRCActorDescription>> PerFrameActorsDeleted;

	/** Presets that had their metadata modified for a frame */
	TSet<FGuid> PerFrameModifiedMetadata;

	/** Presets that had their layout modified for a frame. */
	TSet<FGuid> PerFrameModifiedPresetLayouts;
	
	/** Holds the ID of the client currently making a request. Used to prevent sending back notifications to it. */
	const FGuid& ActingClientId;
	
	/** Frame counter for delaying property change checks. */
	int32 PropertyNotificationFrameCounter = 0;

	/** Handle for when an actor is added to the world. */
	FDelegateHandle OnActorAddedHandle;

	/** Handle for when an actor is deleted from the world. */
	FDelegateHandle OnActorDeletedHandle;

	/** Handle for when the list of actors changes. */
	FDelegateHandle OnActorListChangedHandle;

	/**
	 * Actors that we are actively watching to send events to subscribers.
	 * The key is not a weak pointer, so it shouldn't be accessed in case it's stale. Use the value instead.
	 */
	TMap<AActor*, FWatchedActorData> WatchedActors;
};