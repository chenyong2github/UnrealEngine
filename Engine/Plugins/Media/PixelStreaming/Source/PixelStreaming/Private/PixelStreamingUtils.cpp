// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/CondensedJsonPrintPolicy.h"

namespace UE::PixelStreaming
{
	void ExtendJsonWithField(const FString& Descriptor, FString FieldName, FString StringValue, FString& OutNewDescriptor, bool& OutSuccess)
	{
		TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);

		if (!Descriptor.IsEmpty())
		{
			TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(Descriptor);
			if (!FJsonSerializer::Deserialize(JsonReader, JsonObject) || !JsonObject.IsValid())
			{
				OutSuccess = false;
				return;
			}
		}

		TSharedRef<FJsonValueString> JsonValueObject = MakeShareable(new FJsonValueString(StringValue));
		JsonObject->SetField(FieldName, JsonValueObject);

		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> JsonWriter = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutNewDescriptor);
		OutSuccess = FJsonSerializer::Serialize(JsonObject.ToSharedRef(), JsonWriter);
	}

	void ExtractJsonFromDescriptor(FString Descriptor, FString FieldName, FString& OutStringValue, bool& OutSuccess)
	{
		TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);

		TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(Descriptor);
		if (FJsonSerializer::Deserialize(JsonReader, JsonObject) && JsonObject.IsValid())
		{
			const TSharedPtr<FJsonObject>* JsonObjectPtr = &JsonObject;
			OutSuccess = (*JsonObjectPtr)->TryGetStringField(FieldName, OutStringValue);
		}
		else
		{
			OutSuccess = false;
		}
	}

} // UE::PixelStreaming

