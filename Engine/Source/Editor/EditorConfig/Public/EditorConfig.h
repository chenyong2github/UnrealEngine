// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "JsonConfig.h"
#include "Concepts/EqualityComparable.h"
#include "Dom/JsonObject.h"
#include "Templates/Models.h"

class EDITORCONFIG_API FEditorConfig
{
public:
	FEditorConfig();

	void SetParent(TSharedPtr<FEditorConfig> InConfig);

	bool LoadFromString(FStringView Content);
	bool SaveToString(FString& OutResult) const;

	bool IsValid() const { return JsonConfig.IsValid() && JsonConfig->IsValid(); }

	TSharedPtr<FEditorConfig> GetParentConfig() const;

	// UStruct & UObject
	template <typename T>
	bool TryGetStruct(FStringView Key, T& OutValue) const;
	template <typename T>
	bool TryGetUObject(FStringView Key, T& OutValue) const;

	template <typename T>
	bool TryGetRootStruct(T& OutValue) const;
	template <typename T>
	bool TryGetRootUObject(T& OutValue) const;

	template <typename T>
	void SetStruct(FStringView Key, const T& InValue);
	template <typename T>
	void SetUObject(FStringView Key, const T& InValue);

	template <typename T>
	void SetRootStruct(const T& InValue);
	template <typename T>
	void SetRootUObject(const T& InValue);

	bool HasOverride(FStringView Key) const;

private:

	friend class UEditorConfigSubsystem; // for access to LoadFromFile and SaveToFile

	static void ReadUObject(TSharedPtr<FJsonObject> JsonObject, const UClass* Class, UObject* Instance);
	static void ReadStruct(TSharedPtr<FJsonObject> JsonObject, const UStruct* Struct, void* Instance, UObject* Owner);
	static void ReadValue(TSharedPtr<FJsonValue> JsonValue, const FProperty* Property, void* DataPtr, UObject* Owner);
	
	static TSharedPtr<FJsonObject> WriteStruct(const UStruct* Struct, const void* Instance);
	static TSharedPtr<FJsonObject> WriteUObject(const UStruct* Struct, const UObject* Instance);
	static TSharedPtr<FJsonValue> WriteValue(const FProperty* Property, const void* DataPtr);

	static bool IsDefault(const FProperty* Property, TSharedPtr<FJsonValue> JsonValue, const void* NativeValue);

	bool LoadFromFile(FStringView FilePath);
	bool SaveToFile(FStringView FilePath) const;

private:
	TSharedPtr<UE::FJsonConfig> JsonConfig;
	TSharedPtr<FEditorConfig> ParentConfig;
		
	bool Dirty { false };
};

template <typename T>
bool FEditorConfig::TryGetStruct(FStringView Key, T& OutValue) const
{
	if (!IsValid())
	{
		return false;
	}

	TSharedPtr<FJsonObject> StructData;
	UE::FJsonPath Path(Key);
	if (!JsonConfig->TryGetJsonObject(Path, StructData))
	{
		return false;
	}

	if (!StructData.IsValid())
	{
		return false;
	}

	UStruct* Struct = T::StaticStruct();
	ReadStruct(StructData, Struct, &OutValue, nullptr);

	return true;
}

template <typename T>
bool FEditorConfig::TryGetUObject(FStringView Key, T& OutValue) const
{
	static_assert(TIsDerivedFrom<T, UObject>::Value, "Type is not derived from UObject.");

	if (!IsValid())
	{
		return false;
	}

	TSharedPtr<FJsonObject> UObjectData;
	UE::FJsonPath Path(Key);
	if (!JsonConfig->TryGetJsonObject(Path, UObjectData))
	{
		return false;
	}

	if (!UObjectData.IsValid())
	{
		return false;
	}

	UClass* Class = T::StaticClass();
	ReadUObject(UObjectData, Class, &OutValue);

	return true;
}

template <typename T>
bool FEditorConfig::TryGetRootStruct(T& OutValue) const
{
	if (!IsValid())
	{
		return false;
	}

	TSharedPtr<FJsonObject> StructData = JsonConfig->GetRootObject();

	UStruct* Struct = T::StaticStruct();
	ReadStruct(StructData, Struct, &OutValue, nullptr);

	return true;
}

template <typename T>
bool FEditorConfig::TryGetRootUObject(T& OutValue) const
{
	static_assert(TIsDerivedFrom<T, UObject>::Value, "Type is not derived from UObject.");

	if (!IsValid())
	{
		return false;
	}

	TSharedPtr<FJsonObject> UObjectData = JsonConfig->GetRootObject();

	UClass* Class = T::StaticClass();
	ReadUObject(UObjectData, Class, &OutValue);

	return true;
}

template <typename T>
void FEditorConfig::SetStruct(FStringView Key, const T& InValue)
{
	if (!IsValid())
	{
		return;
	}

	TSharedPtr<FJsonObject> JsonObject = WriteStruct(T::StaticStruct(), &InValue);
	JsonConfig->SetJsonObject(UE::FJsonPath(Key), JsonObject);

	Dirty = true;
}

template <typename T>
void FEditorConfig::SetUObject(FStringView Key, const T& InValue)
{
	static_assert(TIsDerivedFrom<T, UObject>::Value, "Type is not derived from UObject.");

	if (!IsValid())
	{
		return;
	}
	
	TSharedPtr<FJsonObject> JsonObject = WriteStruct(T::StaticClass(), &InValue);
	JsonConfig->SetJsonObject(UE::FJsonPath(Key), JsonObject);
		
	Dirty = true;
}

template <typename T>
void FEditorConfig::SetRootStruct(const T& InValue)
{
	if (!IsValid())
	{
		return;
	}

	TSharedPtr<FJsonObject> JsonObject = WriteStruct(T::StaticStruct(), &InValue);
	JsonConfig->SetRootObject(JsonObject);
		
	Dirty = true;
}

template <typename T>
void FEditorConfig::SetRootUObject(const T& InValue)
{
	static_assert(TIsDerivedFrom<T, UObject>::Value, "Type is not derived from UObject.");

	if (!IsValid())
	{
		return;
	}
	
	TSharedPtr<FJsonObject> JsonObject = WriteStruct(T::StaticStruct(), &InValue);
	JsonConfig->SetRootObject(JsonObject);
		
	Dirty = true;
}
