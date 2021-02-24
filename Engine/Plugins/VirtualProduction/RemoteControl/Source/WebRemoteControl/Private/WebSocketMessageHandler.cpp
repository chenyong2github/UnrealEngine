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

void FWebSocketMessageHandler::HandleWebSocketPresetRegister(const FRemoteControlWebSocketMessage& WebSocketMessage)
{
	FRCWebSocketPresetRegisterBody Body;
	if (!WebRemoteControlUtils::DeserializeRequestPayload(WebSocketMessage.RequestPayload, nullptr, Body))
	{
		return;
	}

	URemoteControlPreset* Preset = IRemoteControlModule::Get().ResolvePreset(*Body.PresetName);
	if (Preset == nullptr)
	{
		return;
	}

	ClientConfigMap.FindOrAdd(WebSocketMessage.ClientId).bIgnoreRemoteChanges = Body.IgnoreRemoteChanges;
	
	TArray<FGuid>* ClientIds = WebSocketNotificationMap.Find(Preset->GetFName());

	// Don't register delegates for a preset more than once.
	if (!ClientIds)
	{
		ClientIds = &WebSocketNotificationMap.Add(Preset->GetFName());

		//Register to any useful callback for the given preset
		Preset->OnExposedPropertyChanged().AddRaw(this, &FWebSocketMessageHandler::OnPresetExposedPropertyChanged);
		Preset->OnPropertyExposed().AddRaw(this, &FWebSocketMessageHandler::OnPropertyExposed);
		Preset->OnPropertyUnexposed().AddRaw(this, &FWebSocketMessageHandler::OnPropertyUnexposed);
		Preset->OnFieldRenamed().AddRaw(this, &FWebSocketMessageHandler::OnFieldRenamed);
		Preset->OnMetadataModified().AddRaw(this, &FWebSocketMessageHandler::OnMetadataModified);
		Preset->OnActorPropertyModified().AddRaw(this, &FWebSocketMessageHandler::OnActorPropertyChanged);

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

	if (TArray<FGuid>* RegisteredClients = WebSocketNotificationMap.Find(*Body.PresetName))
	{
		RegisteredClients->Remove(WebSocketMessage.ClientId);
	}
}

void FWebSocketMessageHandler::ProcessChangedProperties()
{
	//Go over each property that were changed for each preset
	for (const TPair<FName, TMap<FGuid, TArray<FRemoteControlProperty>>>& Entry : PerFramePropertyChanged)
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
		for (const TPair<FGuid, TArray<FRemoteControlProperty>>& ClientToEventsPair : Entry.Value)
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

	PerFramePropertyChanged.Empty();
}

void FWebSocketMessageHandler::ProcessChangedActorProperties()
{
	//Go over each property that were changed for each preset
	for (const TPair<FName, TMap<FGuid, TMap<FRemoteControlActor, TArray<FRCObjectReference>>>>& Entry : PerFrameActorPropertyChanged)
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

void FWebSocketMessageHandler::OnPropertyExposed(URemoteControlPreset* Owner, FName PropertyLabel)
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
	PerFrameAddedProperties.FindOrAdd(Owner->GetFName()).AddUnique(PropertyLabel);
}

void FWebSocketMessageHandler::OnPresetExposedPropertyChanged(URemoteControlPreset* Owner, const FRemoteControlProperty& PropertyChanged)
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
	TMap<FGuid, TArray<FRemoteControlProperty>>& EventsForClient = PerFramePropertyChanged.FindOrAdd(Owner->GetFName());
	// Dont send events to the client that triggered it.
	if (TArray<FGuid>* SubscribedClients = WebSocketNotificationMap.Find(Owner->GetFName()))
	{
		for (const FGuid& Client : *SubscribedClients)
		{
			if (Client != ActingClientId)
			{
				EventsForClient.FindOrAdd(Client).AddUnique(PropertyChanged);
			}
		}
	}
}

void FWebSocketMessageHandler::OnPropertyUnexposed(URemoteControlPreset* Owner, FName PropertyLabel)
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
	PerFrameRemovedProperties.FindOrAdd(Owner->GetFName()).AddUnique(PropertyLabel);
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
	PerFrameRenamedFields.FindOrAdd(Owner->GetFName()).AddUnique(TTuple<FName, FName>(OldFieldLabel, NewFieldLabel));
}

void FWebSocketMessageHandler::OnMetadataModified(URemoteControlPreset* Owner)
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
	if (Owner == nullptr)
	{
		return;
	}

	if (WebSocketNotificationMap.Num() <= 0)
	{
		return;
	}

	//Cache the field that was renamed for end of frame notification
	PerFrameModifiedMetadata.AddUnique(Owner->GetFName());
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
	TMap<FGuid, TMap<FRemoteControlActor, TArray<FRCObjectReference>>>& EventsForClient = PerFrameActorPropertyChanged.FindOrAdd(Owner->GetFName());

	// Dont send events to the client that triggered it.
	if (TArray<FGuid>* SubscribedClients = WebSocketNotificationMap.Find(Owner->GetFName()))
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

	ProcessChangedProperties();
	ProcessChangedActorProperties();
	ProcessRemovedProperties();
	ProcessAddedProperties();
	ProcessRenamedFields();
	ProcessModifiedMetadata();
}

void FWebSocketMessageHandler::ProcessAddedProperties()
{
	for (const TPair<FName, TArray<FName>>& Entry : PerFrameAddedProperties)
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

		TMap<FRemoteControlPresetGroup*, TArray<FName>> GroupedNewFields;

		for (const FName& Label : Entry.Value)
		{
			if (FRemoteControlPresetGroup* Group = Preset->Layout.FindGroupFromField(Preset->GetFieldId(Label)))
			{
				GroupedNewFields.FindOrAdd(Group).Add(Label);
			}
		}

		for (const TTuple<FRemoteControlPresetGroup*, TArray<FName>>& Tuple : GroupedNewFields)
		{
			AddedPropertiesDescription.Groups.Emplace(Preset, *Tuple.Key, Tuple.Value);
		}

		TArray<uint8> Payload;
		WebRemoteControlUtils::SerializeResponse(FRCPresetFieldsAddedEvent{ Entry.Key, AddedPropertiesDescription }, Payload);
		BroadcastToListeners(Entry.Key, Payload);
	}

	PerFrameAddedProperties.Empty();
}

void FWebSocketMessageHandler::ProcessRemovedProperties()
{
	for (const TPair<FName, TArray<FName>>& Entry : PerFrameRemovedProperties)
	{
		if (Entry.Value.Num() <= 0 || !ShouldProcessEventForPreset(Entry.Key))
		{
			continue;
		}

		TArray<uint8> Payload;
		WebRemoteControlUtils::SerializeResponse(FRCPresetFieldsRemovedEvent{ Entry.Key, Entry.Value }, Payload);
		BroadcastToListeners(Entry.Key, Payload);
	}

	PerFrameRemovedProperties.Empty();
}

void FWebSocketMessageHandler::ProcessRenamedFields()
{
	for (const TPair<FName, TArray<TTuple<FName, FName>>>& Entry : PerFrameRenamedFields)
	{
		if (Entry.Value.Num() <= 0 || !ShouldProcessEventForPreset(Entry.Key))
		{
			continue;
		}

		TArray<uint8> Payload;
		WebRemoteControlUtils::SerializeResponse(FRCPresetFieldsRenamedEvent{Entry.Key, Entry.Value}, Payload);
		BroadcastToListeners(Entry.Key, Payload);
	}

	PerFrameRenamedFields.Empty();
}

void FWebSocketMessageHandler::ProcessModifiedMetadata()
{
	for (const FName& Entry : PerFrameModifiedMetadata)
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

void FWebSocketMessageHandler::BroadcastToListeners(FName TargetPresetName, const TArray<uint8>& Payload)
{
	const TArray<FGuid>& Listeners = WebSocketNotificationMap.FindChecked(TargetPresetName);
	for (const FGuid& Listener : Listeners)
	{
		Server->Send(Listener, Payload);
	}
}

bool FWebSocketMessageHandler::ShouldProcessEventForPreset(FName PresetName) const
{
	return WebSocketNotificationMap.Contains(PresetName) && WebSocketNotificationMap[PresetName].Num() > 0;
}

bool FWebSocketMessageHandler::WritePropertyChangeEventPayload(URemoteControlPreset* InPreset, const TArray<FRemoteControlProperty>& InEvents, TArray<uint8>& OutBuffer)
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

		JsonWriter->WriteIdentifierPrefix("ChangedFields");

		//All exposed properties of this preset that changed
		JsonWriter->WriteArrayStart();
		{
			for (const FRemoteControlProperty& Property : InEvents)
			{
				TOptional<FExposedProperty> ExposedProperty = InPreset->ResolveExposedProperty(Property.GetLabel());
				if (ExposedProperty.IsSet())
				{
					bHasProperty = true;

					FRCObjectReference ObjectRef;

					//Property object
					JsonWriter->WriteObjectStart();
					{
						JsonWriter->WriteValue(TEXT("PropertyLabel"), *Property.GetLabel().ToString());

						for (UObject* Object : ExposedProperty->OwnerObjects)
						{
							bHasProperty = true;

							IRemoteControlModule::Get().ResolveObjectProperty(ERCAccess::READ_ACCESS, Object, Property.FieldPathInfo.ToString(), ObjectRef);

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
