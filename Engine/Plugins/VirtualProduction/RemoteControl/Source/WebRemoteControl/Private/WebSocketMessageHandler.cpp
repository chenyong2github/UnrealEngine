// Copyright Epic Games, Inc. All Rights Reserved.

#include "WebSocketMessageHandler.h"

#include "Misc/CoreDelegates.h"
#include "RemoteControlModels.h"
#include "RemoteControlPreset.h"
#include "RemoteControlRequest.h"
#include "RemoteControlRoute.h"
#include "WebRemoteControl.h"
#include "WebRemoteControlUtils.h"


FWebSocketMessageHandler::FWebSocketMessageHandler(FRCWebSocketServer* InServer)
	:Server(InServer)
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

	URemoteControlPreset* Preset = IRemoteControlModule::Get().ResolvePreset(FName(*Body.PresetName));
	if (Preset == nullptr)
	{
		return;
	}
	
	//Register to any useful callback for the given preset
	Preset->OnExposedPropertyChanged().AddRaw(this, &FWebSocketMessageHandler::OnPresetExposedPropertyChanged);
	Preset->OnPropertyExposed().AddRaw(this, &FWebSocketMessageHandler::OnPropertyExposed);
	Preset->OnPropertyUnexposed().AddRaw(this, &FWebSocketMessageHandler::OnPropertyUnexposed);
	Preset->OnFieldRenamed().AddRaw(this, &FWebSocketMessageHandler::OnFieldRenamed);
	WebSocketNotificationMap.FindOrAdd(Preset->GetFName()).AddUnique(WebSocketMessage.ClientId);
}


void FWebSocketMessageHandler::HandleWebSocketPresetUnregister(const FRemoteControlWebSocketMessage& WebSocketMessage)
{
	FRCWebSocketPresetRegisterBody Body;
	if (!WebRemoteControlUtils::DeserializeRequestPayload(WebSocketMessage.RequestPayload, nullptr, Body))
	{
		return;
	}

	if (TArray<FGuid> * RegisteredClients = WebSocketNotificationMap.Find(*Body.PresetName))
	{
		RegisteredClients->Remove(WebSocketMessage.ClientId);
	}
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
	PerFramePropertyChanged.FindOrAdd(Owner->GetFName()).AddUnique(PropertyChanged);
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

void FWebSocketMessageHandler::OnConnectionClosedCallback(FGuid ClientId)
{
	//Cleanup client that were waiting for callbacks
	for (auto Iter = WebSocketNotificationMap.CreateIterator(); Iter; ++Iter)
	{
		Iter.Value().Remove(ClientId);
	}
}

void FWebSocketMessageHandler::OnEndFrame()
{
	//Early exit if no clients are requesting notifications
	if (WebSocketNotificationMap.Num() <= 0)
	{
		return;
	}

	ProcessChangedProperties();
	ProcessRemovedProperties();
	ProcessAddedProperties();
	ProcessRenamedFields();
}

void FWebSocketMessageHandler::ProcessChangedProperties()
{
	//Go over each property that were changed for each preset
	for (const TPair<FName, TArray <FRemoteControlProperty>>& Entry : PerFramePropertyChanged)
	{
		if (!ShouldProcessEventForPreset(Entry.Key))
		{
			continue;
		}

		bool bHasProperty = false;
		TArray<uint8> WorkingBuffer;
		FMemoryWriter Writer(WorkingBuffer);
		TSharedPtr<TJsonWriter<UCS2CHAR>> JsonWriter = TJsonWriter<UCS2CHAR>::Create(&Writer);

		//Resolve the preset in question
		URemoteControlPreset* Preset = IRemoteControlModule::Get().ResolvePreset(Entry.Key);

		//Might be a better idea to have defined structures for our web socket notification messages

		//Response object
		JsonWriter->WriteObjectStart();
		{
			JsonWriter->WriteValue(TEXT("ResponseType"), TEXT("PropertiesChanged"));
			JsonWriter->WriteValue("PresetName", *Preset->GetFName().ToString());

			JsonWriter->WriteIdentifierPrefix("Properties");

			//All exposed properties of this preset that changed
			JsonWriter->WriteArrayStart();
			{
				for (const FRemoteControlProperty& Property : Entry.Value)
				{
					TOptional<FExposedProperty> ExposedProperty = Preset->ResolveExposedProperty(Property.Label);
					if (ExposedProperty.IsSet())
					{
						bHasProperty = true;

						FRCObjectReference ObjectRef;

						//Property object
						JsonWriter->WriteObjectStart();
						{
							JsonWriter->WriteValue(TEXT("PropertyLabel"), *Property.Label.ToString() );

							for (UObject* Object : ExposedProperty->OwnerObjects)
							{
								bHasProperty = true;

								FString FullPath = Property.FieldName.ToString();
								if (!Property.PathRelativeToOwner.IsEmpty())
								{
									FullPath = FString::Printf(TEXT("%s.%s"), *Property.PathRelativeToOwner, *FullPath);
								}
								IRemoteControlModule::Get().ResolveObjectProperty(ERCAccess::READ_ACCESS, Object, FullPath, ObjectRef);

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

		if (bHasProperty)
		{
			TArray<uint8> Payload;
			WebRemoteControlUtils::ConvertToUTF8(WorkingBuffer, Payload);
			BroadcastToListeners(Entry.Key, Payload);
		}
	}

	PerFramePropertyChanged.Empty();
}

void FWebSocketMessageHandler::ProcessAddedProperties()
{
	for (const TPair<FName, TArray<FName>>& Entry : PerFrameAddedProperties)
	{
		if (!ShouldProcessEventForPreset(Entry.Key))
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

		for (const FName& Label : Entry.Value)
		{
			FRemoteControlTargetDescription Description = FRemoteControlTargetDescription(Preset, Label);

			const FName EntryName = Description.Name;
			FRemoteControlTargetDescription* ExistingDescription = AddedPropertiesDescription.ExposedObjects.FindByPredicate([EntryName](const FRemoteControlTargetDescription& Other)
			{
				return Other.Name == EntryName;
			});

			//If we already have an entry for this exposed field, append its data, if not, add the new entry to the list
			if (ExistingDescription)
			{
				ExistingDescription->ExposedFunctions.Append(Description.ExposedFunctions);
				ExistingDescription->ExposedProperties.Append(Description.ExposedProperties);
			}
			else
			{
				AddedPropertiesDescription.ExposedObjects.Emplace(MoveTemp(Description));
			}
		}

		TArray<uint8> WorkingBuffer;
		FMemoryWriter Writer(WorkingBuffer);
		TSharedPtr<TJsonWriter<UCS2CHAR>> JsonWriter = TJsonWriter<UCS2CHAR>::Create(&Writer);

		//Response object
		JsonWriter->WriteObjectStart();
		{
			JsonWriter->WriteValue(TEXT("ResponseType"), TEXT("PropertiesExposed"));
			JsonWriter->WriteIdentifierPrefix("Preset");
			
			FJsonStructSerializerBackend SerializeBackend{ Writer, EStructSerializerBackendFlags::Default };
			FStructSerializer::Serialize(AddedPropertiesDescription, SerializeBackend, FStructSerializerPolicies());
		}
		JsonWriter->WriteObjectEnd();

		TArray<uint8> Payload;
		WebRemoteControlUtils::ConvertToUTF8(WorkingBuffer, Payload);
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

		TArray<uint8> WorkingBuffer;
		FMemoryWriter Writer(WorkingBuffer);
		TSharedPtr<TJsonWriter<UCS2CHAR>> JsonWriter = TJsonWriter<UCS2CHAR>::Create(&Writer);

		//Response object
		JsonWriter->WriteObjectStart();
		{
			JsonWriter->WriteValue(TEXT("ResponseType"), TEXT("PropertiesRemoved"));
			JsonWriter->WriteValue("PresetName", *Entry.Key.ToString());

			JsonWriter->WriteIdentifierPrefix("Properties");

			//All exposed properties of this preset that changed
			JsonWriter->WriteArrayStart();
			{
				for (FName Property : Entry.Value)
				{
					JsonWriter->WriteValue(Property.ToString());
				}
			}
			JsonWriter->WriteArrayEnd();
		}
		JsonWriter->WriteObjectEnd();

		TArray<uint8> Payload;
		WebRemoteControlUtils::ConvertToUTF8(WorkingBuffer, Payload);
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
