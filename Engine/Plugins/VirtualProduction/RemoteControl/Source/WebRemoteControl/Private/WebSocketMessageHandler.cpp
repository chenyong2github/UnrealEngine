// Copyright Epic Games, Inc. All Rights Reserved.

#include "WebSocketMessageHandler.h"

#include "Algo/ForEach.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Guid.h"
#include "IRemoteControlModule.h"
#include "GameFramework/Actor.h"
#include "RemoteControlModels.h"
#include "RemoteControlPreset.h"
#include "RemoteControlRequest.h"
#include "RemoteControlResponse.h"
#include "RemoteControlRoute.h"
#include "WebRemoteControl.h"
#include "WebRemoteControlUtils.h"

static TAutoConsoleVariable<int32> CVarWebRemoteControlFramesBetweenPropertyNotifications(TEXT("WebControl.FramesBetweenPropertyNotifications"), 5, TEXT("The number of frames between sending batches of property notifications."));

FWebSocketMessageHandler::FWebSocketMessageHandler(FRCWebSocketServer* InServer, const FGuid& InActingClientId)
	: Server(InServer)
	, ActingClientId(InActingClientId)
{
	check(Server);
}

void FWebSocketMessageHandler::RegisterRoutes(FWebRemoteControlModule* WebRemoteControl)
{
	FCoreDelegates::OnEndFrame.AddRaw(this, &FWebSocketMessageHandler::OnEndFrame);
	Server->OnConnectionClosed().AddRaw(this, &FWebSocketMessageHandler::OnConnectionClosedCallback);
	
	// WebSocket routes
	TUniquePtr<FRemoteControlWebsocketRoute> RegisterRoute = MakeUnique<FRemoteControlWebsocketRoute>(
		TEXT("Route a message for custom websocket route"),
		TEXT("preset.register"),
		FWebSocketMessageDelegate::CreateRaw(this, &FWebSocketMessageHandler::HandleWebSocketPresetRegister)
		);

	WebRemoteControl->RegisterWebsocketRoute(*RegisterRoute);
	Routes.Emplace(MoveTemp(RegisterRoute));

	TUniquePtr<FRemoteControlWebsocketRoute> UnregisterRoute = MakeUnique<FRemoteControlWebsocketRoute>(
		TEXT("Route a message for custom websocket route"),
		TEXT("preset.unregister"),
		FWebSocketMessageDelegate::CreateRaw(this, &FWebSocketMessageHandler::HandleWebSocketPresetUnregister)
		);

	WebRemoteControl->RegisterWebsocketRoute(*UnregisterRoute);
	Routes.Emplace(MoveTemp(UnregisterRoute));
}

void FWebSocketMessageHandler::UnregisterRoutes(FWebRemoteControlModule* WebRemoteControl)
{
	Server->OnConnectionClosed().RemoveAll(this);
	FCoreDelegates::OnEndFrame.RemoveAll(this);

	for (const TUniquePtr<FRemoteControlWebsocketRoute>& Route : Routes)
	{
		WebRemoteControl->UnregisterWebsocketRoute(*Route);
	}
}

void FWebSocketMessageHandler::NotifyPropertyChangedRemotely(const FGuid& OriginClientId, const FGuid& PresetId, const FGuid& ExposedPropertyId)
{
	if (TArray<FGuid>* SubscribedClients = WebSocketNotificationMap.Find(PresetId))
	{
		if (SubscribedClients->Contains(OriginClientId))
		{
			bool bIgnoreIncomingNotification = false;

			if (FRCClientConfig* Config = ClientConfigMap.Find(OriginClientId))
			{
				bIgnoreIncomingNotification = Config->bIgnoreRemoteChanges;
			}

			if (!bIgnoreIncomingNotification)
			{
				PerFrameModifiedProperties.FindOrAdd(PresetId).FindOrAdd(OriginClientId).Add(ExposedPropertyId);
			}
			else
			{
				for (TPair<FGuid, TSet<FGuid>>& Entry : PerFrameModifiedProperties.FindOrAdd(PresetId))
				{
					if (Entry.Key != OriginClientId)
					{
						Entry.Value.Add(ExposedPropertyId);
					}
				}
			}

			PropertiesManuallyNotifiedThisFrame.Add(ExposedPropertyId);
		}
	}
}

void FWebSocketMessageHandler::HandleWebSocketPresetRegister(const FRemoteControlWebSocketMessage& WebSocketMessage)
{
	FRCWebSocketPresetRegisterBody Body;
	if (!WebRemoteControlUtils::DeserializeRequestPayload(WebSocketMessage.RequestPayload, nullptr, Body))
	{
		return;
	}

	URemoteControlPreset* Preset = nullptr;

	FGuid PresetId;

	if (FGuid::ParseExact(Body.PresetName, EGuidFormats::Digits, PresetId))
	{
		
		Preset = IRemoteControlModule::Get().ResolvePreset(PresetId);
	}
	else
	{
		Preset = IRemoteControlModule::Get().ResolvePreset(*Body.PresetName);
	}


	if (Preset == nullptr)
	{
		return;
	}

	ClientConfigMap.FindOrAdd(WebSocketMessage.ClientId).bIgnoreRemoteChanges = Body.IgnoreRemoteChanges;
	
	TArray<FGuid>* ClientIds = WebSocketNotificationMap.Find(Preset->GetPresetId());

	// Don't register delegates for a preset more than once.
	if (!ClientIds)
	{
		ClientIds = &WebSocketNotificationMap.Add(Preset->GetPresetId());

		//Register to any useful callback for the given preset
		Preset->OnExposedPropertiesModified().AddRaw(this, &FWebSocketMessageHandler::OnPresetExposedPropertiesModified);
		Preset->OnEntityExposed().AddRaw(this, &FWebSocketMessageHandler::OnPropertyExposed);
		Preset->OnEntityUnexposed().AddRaw(this, &FWebSocketMessageHandler::OnPropertyUnexposed);
		Preset->OnFieldRenamed().AddRaw(this, &FWebSocketMessageHandler::OnFieldRenamed);
		Preset->OnMetadataModified().AddRaw(this, &FWebSocketMessageHandler::OnMetadataModified);
		Preset->OnActorPropertyModified().AddRaw(this, &FWebSocketMessageHandler::OnActorPropertyChanged);
		Preset->OnEntitiesUpdated().AddRaw(this, &FWebSocketMessageHandler::OnEntitiesModified);
		Preset->OnPresetLayoutModified().AddRaw(this, &FWebSocketMessageHandler::OnLayoutModified);
	}

	ClientIds->AddUnique(WebSocketMessage.ClientId);
}


void FWebSocketMessageHandler::HandleWebSocketPresetUnregister(const FRemoteControlWebSocketMessage& WebSocketMessage)
{
	FRCWebSocketPresetRegisterBody Body;
	if (!WebRemoteControlUtils::DeserializeRequestPayload(WebSocketMessage.RequestPayload, nullptr, Body))
	{
		return;
	}

	URemoteControlPreset* Preset = nullptr;

	FGuid PresetId;

	if (FGuid::ParseExact(Body.PresetName, EGuidFormats::Digits, PresetId))
	{
		Preset = IRemoteControlModule::Get().ResolvePreset(PresetId);
	}
	else
	{
		Preset = IRemoteControlModule::Get().ResolvePreset(*Body.PresetName);
	}

	if (Preset)
	{
		if (TArray<FGuid>* RegisteredClients = WebSocketNotificationMap.Find(Preset->GetPresetId()))
		{
			RegisteredClients->Remove(WebSocketMessage.ClientId);
		}
	}
}

void FWebSocketMessageHandler::ProcessChangedProperties()
{
	//Go over each property that were changed for each preset
	for (const TPair<FGuid, TMap<FGuid, TSet<FGuid>>>& Entry : PerFrameModifiedProperties)
	{
		if (!ShouldProcessEventForPreset(Entry.Key) || !Entry.Value.Num())
		{
			continue;
		}

		URemoteControlPreset* Preset = IRemoteControlModule::Get().ResolvePreset(Entry.Key);
		if (!Preset)
		{
			continue;
		}

		UE_LOG(LogRemoteControl, VeryVerbose, TEXT("(%s) Broadcasting properties changed event."), *Preset->GetName());

		// Each client will have a custom payload that doesnt contain the events it triggered.
		for (const TPair<FGuid, TSet<FGuid>>& ClientToEventsPair : Entry.Value)
		{
			TArray<uint8> WorkingBuffer;
			if (ClientToEventsPair.Value.Num() && WritePropertyChangeEventPayload(Preset, ClientToEventsPair.Value, WorkingBuffer))
			{
				TArray<uint8> Payload;
				WebRemoteControlUtils::ConvertToUTF8(WorkingBuffer, Payload);
				Server->Send(ClientToEventsPair.Key, Payload);
			}
		}
	}

	PerFrameModifiedProperties.Empty();
}

void FWebSocketMessageHandler::ProcessChangedActorProperties()
{
	//Go over each property that were changed for each preset
	for (const TPair<FGuid, TMap<FGuid, TMap<FRemoteControlActor, TArray<FRCObjectReference>>>>& Entry : PerFrameActorPropertyChanged)
	{
		if (!ShouldProcessEventForPreset(Entry.Key) || !Entry.Value.Num())
		{
			continue;
		}

		URemoteControlPreset* Preset = IRemoteControlModule::Get().ResolvePreset(Entry.Key);
		if (!Preset)
		{
			continue;
		}

		// Each client will have a custom payload that doesnt contain the events it triggered.
		for (const TPair<FGuid, TMap<FRemoteControlActor, TArray<FRCObjectReference>>>& ClientToModifications : Entry.Value)
		{
			TArray<uint8> WorkingBuffer;
			if (ClientToModifications.Value.Num() && WriteActorPropertyChangePayload(Preset, ClientToModifications.Value, WorkingBuffer))
			{
				TArray<uint8> Payload;
				WebRemoteControlUtils::ConvertToUTF8(WorkingBuffer, Payload);
				Server->Send(ClientToModifications.Key, Payload);
			}
		}
	}

	PerFrameActorPropertyChanged.Empty();
}

void FWebSocketMessageHandler::OnPropertyExposed(URemoteControlPreset* Owner, const FGuid& EntityId)
{
	if (Owner == nullptr)
	{
		return;
	}

	if (WebSocketNotificationMap.Num() <= 0)
	{
		return;
	}

	//Cache the property field that was removed for end of frame notification
	PerFrameAddedProperties.FindOrAdd(Owner->GetPresetId()).AddUnique(EntityId);
}

void FWebSocketMessageHandler::OnPresetExposedPropertiesModified(URemoteControlPreset* Owner, const TSet<FGuid>& ModifiedPropertyIds)
{
	if (Owner == nullptr)
	{
		return;
	}

	if (WebSocketNotificationMap.Num() <= 0)
	{
		return;
	}

	//Cache the property field that changed for end of frame notification
	TMap<FGuid, TSet<FGuid>>& EventsForClient = PerFrameModifiedProperties.FindOrAdd(Owner->GetPresetId());
	
	if (TArray<FGuid>* SubscribedClients = WebSocketNotificationMap.Find(Owner->GetPresetId()))
	{
		for (const FGuid& ModifiedPropertyId : ModifiedPropertyIds)
		{
			// Don't send a change notification if the change was manually notified.
			// This is to avoid the case of a post edit change property being caught by the preset for a change 
			// that a client deliberatly wishes to ignore.
			if (!PropertiesManuallyNotifiedThisFrame.Contains(ModifiedPropertyId))
			{
				for (const FGuid& Client : *SubscribedClients)
				{
					if (Client != ActingClientId || !ClientConfigMap.FindChecked(Client).bIgnoreRemoteChanges)
					{
						EventsForClient.FindOrAdd(Client).Append(ModifiedPropertyIds);
					}
				}
			}
			else
			{
				// Remove the property after encountering it here since we can't remove it on end frame
				// because that might happen before the final PostEditChange of a property change in the RC Module.
				PropertiesManuallyNotifiedThisFrame.Remove(ModifiedPropertyId);
			}
		}
	}
}

void FWebSocketMessageHandler::OnPropertyUnexposed(URemoteControlPreset* Owner, const FGuid& EntityId)
{
	if (Owner == nullptr)
	{
		return;
	}

	if (WebSocketNotificationMap.Num() <= 0)
	{
		return;
	}

	TSharedPtr<FRemoteControlEntity> Entity = Owner->GetExposedEntity(EntityId).Pin();
	check(Entity);

	// Cache the property field that was removed for end of frame notification
	TTuple<TArray<FGuid>, TArray<FName>>& Entries = PerFrameRemovedProperties.FindOrAdd(Owner->GetPresetId());
	Entries.Key.AddUnique(EntityId);
	Entries.Value.AddUnique(Entity->GetLabel());
}

void FWebSocketMessageHandler::OnFieldRenamed(URemoteControlPreset* Owner, FName OldFieldLabel, FName NewFieldLabel)
{
	if (Owner == nullptr)
	{
		return;
	}

	if (WebSocketNotificationMap.Num() <= 0)
	{
		return;
	}

	//Cache the field that was renamed for end of frame notification
	PerFrameRenamedFields.FindOrAdd(Owner->GetPresetId()).AddUnique(TTuple<FName, FName>(OldFieldLabel, NewFieldLabel));
}

void FWebSocketMessageHandler::OnMetadataModified(URemoteControlPreset* Owner)
{
	//Cache the field that was renamed for end of frame notification
	if (Owner == nullptr)
	{
		return;
	}

	if (WebSocketNotificationMap.Num() <= 0)
	{
		return;
	}

	//Cache the field that was renamed for end of frame notification
	PerFrameModifiedMetadata.Add(Owner->GetPresetId());
}

void FWebSocketMessageHandler::OnActorPropertyChanged(URemoteControlPreset* Owner, FRemoteControlActor& Actor, UObject* ModifiedObject, FProperty* ModifiedProperty)
{
	if (Owner == nullptr)
	{
		return;
	}

	if (WebSocketNotificationMap.Num() <= 0)
	{
		return;
	}

	FRCFieldPathInfo FieldPath { ModifiedProperty->GetName() };
	if (!FieldPath.Resolve(ModifiedObject))
	{
		return;
	}

	FRCObjectReference Ref;
	Ref.Object = ModifiedObject;
	Ref.Property = ModifiedProperty;
	Ref.ContainerAdress = FieldPath.GetResolvedData().ContainerAddress;
	Ref.ContainerType = FieldPath.GetResolvedData().Struct;
	Ref.PropertyPathInfo = MoveTemp(FieldPath);
	Ref.Access = ERCAccess::READ_ACCESS;


	//Cache the property field that changed for end of frame notification
	TMap<FGuid, TMap<FRemoteControlActor, TArray<FRCObjectReference>>>& EventsForClient = PerFrameActorPropertyChanged.FindOrAdd(Owner->GetPresetId());

	// Dont send events to the client that triggered it.
	if (TArray<FGuid>* SubscribedClients = WebSocketNotificationMap.Find(Owner->GetPresetId()))
	{
		for (const FGuid& Client : *SubscribedClients)
		{
			if (Client != ActingClientId)
			{
				TMap<FRemoteControlActor, TArray<FRCObjectReference>>& ModifiedPropertiesPerActor = EventsForClient.FindOrAdd(Client);
				ModifiedPropertiesPerActor.FindOrAdd(Actor).AddUnique(Ref);
			}
		}
	}
}

void FWebSocketMessageHandler::OnEntitiesModified(URemoteControlPreset* Owner, const TSet<FGuid>& ModifiedEntities)
{
	// We do not need to store these event for the current frame since this was already handled by the preset in this case.
	if (!Owner || ModifiedEntities.Num() == 0)
	{
		return;
	}
	
	TArray<uint8> Payload;
	WebRemoteControlUtils::SerializeResponse(FRCPresetEntitiesModifiedEvent{Owner, ModifiedEntities.Array()}, Payload);
	BroadcastToListeners(Owner->GetPresetId(), Payload);
}

void FWebSocketMessageHandler::OnLayoutModified(URemoteControlPreset* Owner)
{
	if (Owner == nullptr)
	{
		return;
	}

	if (WebSocketNotificationMap.Num() <= 0)
	{
		return;
	}

	//Cache the field that was renamed for end of frame notification
	PerFrameModifiedPresetLayouts.Add(Owner->GetPresetId());
}

void FWebSocketMessageHandler::OnConnectionClosedCallback(FGuid ClientId)
{
	//Cleanup client that were waiting for callbacks
	for (auto Iter = WebSocketNotificationMap.CreateIterator(); Iter; ++Iter)
	{
		Iter.Value().Remove(ClientId);
	}

	/** Remove this client's config. */
	ClientConfigMap.Remove(ClientId);
}

void FWebSocketMessageHandler::OnEndFrame()
{
	//Early exit if no clients are requesting notifications
	if (WebSocketNotificationMap.Num() <= 0)
	{
		return;
	}

	PropertyNotificationFrameCounter++;

	if (PropertyNotificationFrameCounter >= CVarWebRemoteControlFramesBetweenPropertyNotifications.GetValueOnGameThread())
	{
		PropertyNotificationFrameCounter = 0;
		ProcessChangedProperties();
		ProcessChangedActorProperties();
		ProcessRemovedProperties();
		ProcessAddedProperties();
		ProcessRenamedFields();
		ProcessModifiedMetadata();
		ProcessModifiedPresetLayouts();
	}
}

void FWebSocketMessageHandler::ProcessAddedProperties()
{
	for (const TPair<FGuid, TArray<FGuid>>& Entry : PerFrameAddedProperties)
	{
		if (Entry.Value.Num() <= 0 || !ShouldProcessEventForPreset(Entry.Key))
		{
			continue;
		}

		URemoteControlPreset* Preset = IRemoteControlModule::Get().ResolvePreset(Entry.Key);
		if (Preset == nullptr)
		{
			continue;
		}

		FRCPresetDescription AddedPropertiesDescription;
		AddedPropertiesDescription.Name = Preset->GetName();
		AddedPropertiesDescription.Path = Preset->GetPathName();
		AddedPropertiesDescription.ID = Preset->GetPresetId().ToString();

		TMap<FRemoteControlPresetGroup*, TArray<FGuid>> GroupedNewFields;

		for (const FGuid& Id : Entry.Value)
		{
			if (FRemoteControlPresetGroup* Group = Preset->Layout.FindGroupFromField(Id))
			{
				GroupedNewFields.FindOrAdd(Group).Add(Id);
			}
		}

		for (const TTuple<FRemoteControlPresetGroup*, TArray<FGuid>>& Tuple : GroupedNewFields)
		{
			AddedPropertiesDescription.Groups.Emplace(Preset, *Tuple.Key, Tuple.Value);
		}

		TArray<uint8> Payload;
		WebRemoteControlUtils::SerializeResponse(FRCPresetFieldsAddedEvent{ Preset->GetFName(), Preset->GetPresetId(), AddedPropertiesDescription }, Payload);
		BroadcastToListeners(Entry.Key, Payload);
	}

	PerFrameAddedProperties.Empty();
}

void FWebSocketMessageHandler::ProcessRemovedProperties()
{
	for (const TPair<FGuid, TTuple<TArray<FGuid>, TArray<FName>>>& Entry : PerFrameRemovedProperties)
	{
		if (Entry.Value.Key.Num() <= 0 || !ShouldProcessEventForPreset(Entry.Key))
		{
			continue;
		}

		URemoteControlPreset* Preset = IRemoteControlModule::Get().ResolvePreset(Entry.Key);
		if (Preset == nullptr)
		{
			continue;
		}

		ensure(Entry.Value.Key.Num() == Entry.Value.Value.Num());
		
		TArray<uint8> Payload;
		WebRemoteControlUtils::SerializeResponse(FRCPresetFieldsRemovedEvent{ Preset->GetFName(), Preset->GetPresetId(), Entry.Value.Value, Entry.Value.Key }, Payload);
		BroadcastToListeners(Entry.Key, Payload);
	}
	
	PerFrameRemovedProperties.Empty();
}

void FWebSocketMessageHandler::ProcessRenamedFields()
{
	for (const TPair<FGuid, TArray<TTuple<FName, FName>>>& Entry : PerFrameRenamedFields)
	{
		if (Entry.Value.Num() <= 0 || !ShouldProcessEventForPreset(Entry.Key))
		{
			continue;
		}

		URemoteControlPreset* Preset = IRemoteControlModule::Get().ResolvePreset(Entry.Key);
		if (Preset == nullptr)
		{
			continue;
		}

		TArray<uint8> Payload;
		WebRemoteControlUtils::SerializeResponse(FRCPresetFieldsRenamedEvent{Preset->GetFName(), Preset->GetPresetId(), Entry.Value}, Payload);
		BroadcastToListeners(Entry.Key, Payload);
	}

	PerFrameRenamedFields.Empty();
}

void FWebSocketMessageHandler::ProcessModifiedMetadata()
{
	for (const FGuid& Entry : PerFrameModifiedMetadata)
	{
		if (ShouldProcessEventForPreset(Entry))
		{
			if (URemoteControlPreset* Preset = IRemoteControlModule::Get().ResolvePreset(Entry))
			{
				TArray<uint8> Payload;
				WebRemoteControlUtils::SerializeResponse(FRCPresetMetadataModified{ Preset }, Payload);
				BroadcastToListeners(Entry, Payload);
			}
		}
	}

	PerFrameModifiedMetadata.Empty();
}

void FWebSocketMessageHandler::ProcessModifiedPresetLayouts()
{
	for (const FGuid& Entry : PerFrameModifiedPresetLayouts)
	{
		if (ShouldProcessEventForPreset(Entry))
		{
			if (URemoteControlPreset* Preset = IRemoteControlModule::Get().ResolvePreset(Entry))
			{
				TArray<uint8> Payload;
				WebRemoteControlUtils::SerializeResponse(FRCPresetLayoutModified{ Preset }, Payload);
				BroadcastToListeners(Entry, Payload);
			}
		}
	}

	PerFrameModifiedPresetLayouts.Empty();
}

void FWebSocketMessageHandler::BroadcastToListeners(const FGuid& TargetPresetId, const TArray<uint8>& Payload)
{
	const TArray<FGuid>& Listeners = WebSocketNotificationMap.FindChecked(TargetPresetId);
	for (const FGuid& Listener : Listeners)
	{
		Server->Send(Listener, Payload);
	}
}

bool FWebSocketMessageHandler::ShouldProcessEventForPreset(const FGuid& PresetId) const
{
	return WebSocketNotificationMap.Contains(PresetId) && WebSocketNotificationMap[PresetId].Num() > 0;
}

bool FWebSocketMessageHandler::WritePropertyChangeEventPayload(URemoteControlPreset* InPreset, const TSet<FGuid>& InModifiedPropertyIds, TArray<uint8>& OutBuffer)
{
	bool bHasProperty = false;

	FMemoryWriter Writer(OutBuffer);
	TSharedPtr<TJsonWriter<UCS2CHAR>> JsonWriter = TJsonWriter<UCS2CHAR>::Create(&Writer);

	//Might be a better idea to have defined structures for our web socket notification messages

	//Response object
	JsonWriter->WriteObjectStart();
	{
		JsonWriter->WriteValue(TEXT("Type"), TEXT("PresetFieldsChanged"));
		JsonWriter->WriteValue("PresetName", *InPreset->GetFName().ToString());
		JsonWriter->WriteValue("PresetId", *InPreset->GetPresetId().ToString());

		JsonWriter->WriteIdentifierPrefix("ChangedFields");

		//All exposed properties of this preset that changed
		JsonWriter->WriteArrayStart();
		{
			for (const FGuid& RCPropertyId : InModifiedPropertyIds)
			{
				bHasProperty = true;

				FRCObjectReference ObjectRef;
				if (TSharedPtr<FRemoteControlProperty> RCProperty = InPreset->GetExposedEntity<FRemoteControlProperty>(RCPropertyId).Pin())
				{
					//Property object
					JsonWriter->WriteObjectStart();
					{
						JsonWriter->WriteValue(TEXT("PropertyLabel"), *RCProperty->GetLabel().ToString());
						JsonWriter->WriteValue(TEXT("Id"), *RCProperty->GetId().ToString());

						for (UObject* Object : RCProperty->GetBoundObjects())
						{
							bHasProperty = true;

							IRemoteControlModule::Get().ResolveObjectProperty(ERCAccess::READ_ACCESS, Object, RCProperty->FieldPathInfo.ToString(), ObjectRef);

							JsonWriter->WriteValue(TEXT("ObjectPath"), Object->GetPathName());
							JsonWriter->WriteIdentifierPrefix(TEXT("PropertyValue"));

							RemotePayloadSerializer::SerializePartial(
								[&ObjectRef](FJsonStructSerializerBackend& SerializerBackend)
								{
									return IRemoteControlModule::Get().GetObjectProperties(ObjectRef, SerializerBackend);
								}
							, Writer);

						}
					}
					JsonWriter->WriteObjectEnd();
				}

			}
		}
		JsonWriter->WriteArrayEnd();
	}
	JsonWriter->WriteObjectEnd();

	return bHasProperty;
}


bool FWebSocketMessageHandler::WriteActorPropertyChangePayload(URemoteControlPreset* InPreset, const TMap<FRemoteControlActor, TArray<FRCObjectReference>>& InModifications, TArray<uint8>& OutBuffer)
{
	bool bHasProperty = false;

	FMemoryWriter Writer(OutBuffer);
	TSharedPtr<TJsonWriter<UCS2CHAR>> JsonWriter = TJsonWriter<UCS2CHAR>::Create(&Writer);

	//Might be a better idea to have defined structures for our web socket notification messages

	//Response object
	JsonWriter->WriteObjectStart();
	{
		JsonWriter->WriteValue(TEXT("Type"), TEXT("PresetActorModified"));
		JsonWriter->WriteValue("PresetName", *InPreset->GetFName().ToString());
		JsonWriter->WriteValue("PresetId", *InPreset->GetPresetId().ToString());

		JsonWriter->WriteIdentifierPrefix("ModifiedActors");

		JsonWriter->WriteArrayStart();

		for (const TPair<FRemoteControlActor, TArray<FRCObjectReference>>& Pair : InModifications)
		{
			JsonWriter->WriteObjectStart();
			
			AActor* ModifiedActor = Cast<AActor>(Pair.Key.Path.ResolveObject());
			if (!ModifiedActor)
			{
				continue;
			}

			FString RCActorName = Pair.Key.GetLabel().ToString();
			
			JsonWriter->WriteValue(TEXT("Id"), *Pair.Key.GetId().ToString());
			JsonWriter->WriteValue(TEXT("DisplayName"), RCActorName);
			JsonWriter->WriteValue(TEXT("Path"), Pair.Key.Path.ToString());

			JsonWriter->WriteIdentifierPrefix("ModifiedProperties");
			//All exposed properties of this preset that changed
			JsonWriter->WriteArrayStart();
			{
				for (const FRCObjectReference& Ref : Pair.Value)
				{
					const FProperty* Property = Ref.Property.Get();

					if (!Property || !Ref.IsValid())
					{
						continue;
					}

					bHasProperty = true;

					//Property object
					JsonWriter->WriteObjectStart();
					{
						JsonWriter->WriteValue(TEXT("PropertyName"), Property->GetName());
						JsonWriter->WriteIdentifierPrefix(TEXT("PropertyValue"));
						RemotePayloadSerializer::SerializePartial(
							[&Ref](FJsonStructSerializerBackend& SerializerBackend)
							{
								return IRemoteControlModule::Get().GetObjectProperties(Ref, SerializerBackend);
							}
						, Writer);

					}
					JsonWriter->WriteObjectEnd();
				}
			}
			JsonWriter->WriteArrayEnd();
			JsonWriter->WriteObjectEnd();
		}
		JsonWriter->WriteArrayEnd();

	}
	JsonWriter->WriteObjectEnd();

	return bHasProperty;
}
