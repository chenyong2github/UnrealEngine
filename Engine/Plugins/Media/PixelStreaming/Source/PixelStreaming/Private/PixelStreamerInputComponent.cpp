// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamerInputComponent.h"
#include "IPixelStreamingModule.h"
#include "InputDevice.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "GameFramework/GameUserSettings.h"
#include "PixelStreamingPrivate.h"

UPixelStreamerInputComponent::UPixelStreamerInputComponent()
	: PixelStreamingModule(FModuleManager::Get().GetModulePtr<IPixelStreamingModule>("PixelStreaming"))
{
}

void UPixelStreamerInputComponent::BeginPlay()
{
	Super::BeginPlay();

	if(this->PixelStreamingModule)
	{
		// When this component is initializing it registers itself with the Pixel Streaming module.
		this->PixelStreamingModule->AddInputComponent(this);
	}
	else 
	{
		UE_LOG(PixelStreamer, Warning, TEXT("Pixel Streaming input component not added because Pixel Streaming module is not loaded. This is expected on dedicated servers."));
	}
	
}

void UPixelStreamerInputComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	if(this->PixelStreamingModule) 
	{
		// When this component is destructing it unregisters itself with the Pixel Streaming module.
		this->PixelStreamingModule->RemoveInputComponent(this);
	}
	else 
	{
		UE_LOG(PixelStreamer, Warning, TEXT("Pixel Streaming input component not removed because Pixel Streaming module is not loaded. This is expected on dedicated servers."));
	}
	
}

bool UPixelStreamerInputComponent::OnCommand(const FString& Descriptor)
{
	FString ConsoleCommand;
	bool bSuccess = false;
	GetJsonStringValue(Descriptor, TEXT("ConsoleCommand"), ConsoleCommand, bSuccess);
	if (bSuccess)
	{
		return GEngine->Exec(GetWorld(), *ConsoleCommand);
	}
	
	FString WidthString;
	FString HeightString;
	GetJsonStringValue(Descriptor, TEXT("Resolution.Width"), WidthString, bSuccess);
	if (bSuccess)
	{
		GetJsonStringValue(Descriptor, TEXT("Resolution.Height"), HeightString, bSuccess);

		int Width = FCString::Atoi(*WidthString);
		int Height = FCString::Atoi(*HeightString);

		if(Width < 1 || Height < 1)
		{
			return false;
		}

		FString ChangeResCommand = FString::Printf(TEXT("r.SetRes %dx%d"), Width, Height);
		return GEngine->Exec(GetWorld(), *ChangeResCommand);

	}

	return false;	

}

void UPixelStreamerInputComponent::SendPixelStreamingResponse(const FString& Descriptor)
{
	if(this->PixelStreamingModule)
	{
		PixelStreamingModule->SendResponse(Descriptor);
	}
	else 
	{
		UE_LOG(PixelStreamer, Warning, TEXT("Pixel Streaming input component skipped sending response. This is expected on dedicated servers."));
	}
	
}

void UPixelStreamerInputComponent::GetJsonStringValue(FString Descriptor, FString FieldName, FString& StringValue, bool& Success)
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);

	TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(Descriptor);
	if (FJsonSerializer::Deserialize(JsonReader, JsonObject) && JsonObject.IsValid())
	{
		const TSharedPtr<FJsonObject>* JsonObjectPtr = &JsonObject;

		if (FieldName.Contains(TEXT(".")))
		{
			TArray<FString> FieldComponents;
			FieldName.ParseIntoArray(FieldComponents, TEXT("."));
			FieldName = FieldComponents.Pop();

			for (const FString& FieldComponent : FieldComponents)
			{
				if (!(*JsonObjectPtr)->TryGetObjectField(FieldComponent, JsonObjectPtr))
				{
					Success = false;
					return;
				}
			}
		}

		Success = (*JsonObjectPtr)->TryGetStringField(FieldName, StringValue);
	}
	else
	{
		Success = false;
	}
}

void UPixelStreamerInputComponent::AddJsonStringValue(const FString& Descriptor, FString FieldName, FString StringValue, FString& NewDescriptor, bool& Success)
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);

	if (!Descriptor.IsEmpty())
	{
		TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(Descriptor);
		if (!FJsonSerializer::Deserialize(JsonReader, JsonObject) || !JsonObject.IsValid())
		{
			Success = false;
			return;
		}
	}
	
	TSharedRef<FJsonValueString> JsonValueObject = MakeShareable(new FJsonValueString(StringValue));
	JsonObject->SetField(FieldName, JsonValueObject);

	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> JsonWriter = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&NewDescriptor);
	Success = FJsonSerializer::Serialize(JsonObject.ToSharedRef(), JsonWriter);
}
