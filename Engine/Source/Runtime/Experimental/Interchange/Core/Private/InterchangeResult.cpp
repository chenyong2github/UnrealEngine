// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeResult.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// @TODO: move these methods out of UInterchangeResult and into InterchangeDispatcherTask?
// Is it useful to expose this functionality at the engine level?

FString UInterchangeResult::ToJson()
{
	TSharedPtr<FJsonObject> MsgObject = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> PropertiesObject = MakeShared<FJsonObject>();

	MsgObject->SetStringField(TEXT("Type"), GetClass()->GetName());

	TArray<TSharedPtr<FJsonValue>> Properties;
	for (TFieldIterator<FProperty> FieldIterator(GetClass()); FieldIterator; ++FieldIterator)
	{
		FProperty* Field = *FieldIterator;
		FString PropertyName = Field->GetNameCPP();
		FString PropertyValue;
		Field->ExportTextItem(PropertyValue, Field->ContainerPtrToValuePtr<uint8>(this), nullptr, this, 0);

		TSharedPtr<FJsonObject> Property = MakeShared<FJsonObject>();
		Property->SetStringField(TEXT("Name"), PropertyName);
		Property->SetStringField(TEXT("Value"), PropertyValue);
		Properties.Add(MakeShared<FJsonValueObject>(Property));
	}

	MsgObject->SetArrayField(TEXT("Properties"), Properties);

	FString JsonString;
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> JsonWriter = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonString);
	if (!FJsonSerializer::Serialize(MsgObject.ToSharedRef(), JsonWriter))
	{
		// Error creating the json string 
		return FString();
	}
	return JsonString;
}


UInterchangeResult* UInterchangeResult::FromJson(const FString& JsonString)
{
	TSharedRef<TJsonReader<TCHAR>> Reader = FJsonStringReader::Create(JsonString);

	TSharedPtr<FJsonObject> MsgObject;
	if (!FJsonSerializer::Deserialize(Reader, MsgObject) || !MsgObject.IsValid())
	{
		// Cannot read the json file
		return nullptr;
	}

	FString ClassType;
	if (!MsgObject->TryGetStringField(TEXT("Type"), ClassType))
	{
		// The Json type id key is missing
		return nullptr;
	}

	const TArray<TSharedPtr<FJsonValue>>* Properties = nullptr;
	if (!MsgObject->TryGetArrayField(TEXT("Properties"), Properties))
	{
		// The Json properties array is missing
		return nullptr;
	}

	UClass* ClassToImport = FindObject<UClass>(ANY_PACKAGE, *ClassType);	// @TODO: no idea how we should do this without ANY_PACKAGE
	UInterchangeResult* Result = NewObject<UInterchangeResult>(GetTransientPackage(), ClassToImport);

	for (const TSharedPtr<FJsonValue>& Property : *Properties)
	{
		const TSharedPtr<FJsonObject>& PropertyObject = Property->AsObject();

		FString PropertyName;
		FString PropertyValue;

		if (!PropertyObject->TryGetStringField(TEXT("Name"), PropertyName))
		{
			return nullptr;
		}

		if (!PropertyObject->TryGetStringField(TEXT("Value"), PropertyValue))
		{
			return nullptr;
		}

		FProperty* Field = ClassToImport->FindPropertyByName(FName(*PropertyName));
		Field->ImportText(*PropertyValue, Field->ContainerPtrToValuePtr<uint8>(Result), 0, Result);
	}

	return Result;
}
