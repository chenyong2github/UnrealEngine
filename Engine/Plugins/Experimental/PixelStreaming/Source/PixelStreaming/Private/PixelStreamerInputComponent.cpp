// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PixelStreamerInputComponent.h"
#include "IPixelStreamingModule.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "GameFramework/GameUserSettings.h"

//extern TAutoConsoleVariable<float> CVarStreamerBitrateReduction;

UPixelStreamerInputComponent::UPixelStreamerInputComponent()
	: PixelStreamingModule(FModuleManager::Get().GetModulePtr<IPixelStreamingModule>("PixelStreaming"))
{
}

bool UPixelStreamerInputComponent::OnCommand(const FString& Descriptor)
{
	FString ConsoleCommand;
	bool bSuccess = false;
	GetJsonStringValue(Descriptor, TEXT("ConsoleCommand"), ConsoleCommand, bSuccess);
	if (!bSuccess)
	{
		return GEngine->Exec(GetWorld(), *ConsoleCommand);
	}
	
	FString WidthString;
	FString HeightString;
	GetJsonStringValue(Descriptor, TEXT("Resolution.Width"), WidthString, bSuccess);
	if (bSuccess)
	{
		GetJsonStringValue(Descriptor, TEXT("Resolution.Height"), HeightString, bSuccess);
	}
	if (!bSuccess)
	{
		return false;
	}

	FIntPoint Resolution = { FCString::Atoi(*WidthString), FCString::Atoi(*HeightString) };
	GEngine->GameUserSettings->SetScreenResolution(Resolution);
	GEngine->GameUserSettings->ApplySettings(false);
	return true;
}

void UPixelStreamerInputComponent::SendPixelStreamingResponse(const FString& Descriptor)
{
	PixelStreamingModule->SendResponse(Descriptor);
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
