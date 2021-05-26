// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "JsonConfig.h"
#include "Concepts/EqualityComparable.h"
#include "Dom/JsonObject.h"
#include "Templates/Models.h"

class EDITORCONFIG_API FEditorConfig
{
public:
	enum class EPropertyFilter
	{
		All,
		MetadataOnly
	};

	FEditorConfig();

	void SetParent(TSharedPtr<FEditorConfig> InConfig);

	bool LoadFromString(FStringView Content);
	bool SaveToString(FString& OutResult) const;

	bool IsValid() const { return JsonConfig.IsValid() && JsonConfig->IsValid(); }

	TSharedPtr<FEditorConfig> GetParentConfig() const;

	// UStruct & UObject
	template <typename T>
	bool TryGetStruct(FStringView Key, T& OutValue, EPropertyFilter Filter = EPropertyFilter::MetadataOnly) const;
	template <typename T>
	bool TryGetUObject(FStringView Key, T& OutValue, EPropertyFilter Filter = EPropertyFilter::MetadataOnly) const;

	template <typename T>
	bool TryGetRootStruct(T& OutValue, EPropertyFilter Filter = EPropertyFilter::MetadataOnly) const;
	template <typename T>
	bool TryGetRootUObject(T& OutValue, EPropertyFilter Filter = EPropertyFilter::MetadataOnly) const;

	bool TryGetRootStruct(const UStruct* Class, void* OutValue, EPropertyFilter Filter = EPropertyFilter::MetadataOnly) const;
	bool TryGetRootUObject(const UClass* Class, UObject* OutValue, EPropertyFilter Filter = EPropertyFilter::MetadataOnly) const;

	template <typename T>
	void SetStruct(FStringView Key, const T& InValue, EPropertyFilter Filter = EPropertyFilter::MetadataOnly);
	template <typename T>
	void SetUObject(FStringView Key, const T& InValue, EPropertyFilter Filter = EPropertyFilter::MetadataOnly);

	template <typename T>
	void SetRootStruct(const T& InValue, EPropertyFilter Filter = EPropertyFilter::MetadataOnly);
	template <typename T>
	void SetRootUObject(const T& InValue, EPropertyFilter Filter = EPropertyFilter::MetadataOnly);

	void SetRootStruct(const UStruct* Class, const void* Instance, EPropertyFilter Filter = EPropertyFilter::MetadataOnly);
	void SetRootUObject(const UClass* Class, const UObject* Instance, EPropertyFilter Filter = EPropertyFilter::MetadataOnly);

	bool HasOverride(FStringView Key) const;

	void OnSaved();

	DECLARE_EVENT_OneParam(FEditorConfig, FOnEditorConfigDirtied, const FEditorConfig&);
	FOnEditorConfigDirtied& OnEditorConfigDirtied() { return EditorConfigDirtiedEvent; }

private:

	friend class UEditorConfigSubsystem; // for access to LoadFromFile and SaveToFile

	static void ReadUObject(TSharedPtr<FJsonObject> JsonObject, const UClass* Class, UObject* Instance, EPropertyFilter Filter);
	static void ReadStruct(TSharedPtr<FJsonObject> JsonObject, const UStruct* Struct, void* Instance, UObject* Owner, EPropertyFilter Filter);
	static void ReadValue(TSharedPtr<FJsonValue> JsonValue, const FProperty* Property, void* DataPtr, UObject* Owner);
	
	static TSharedPtr<FJsonObject> WriteStruct(const UStruct* Struct, const void* Instance, EPropertyFilter Filter);
	static TSharedPtr<FJsonObject> WriteUObject(const UClass* Class, const UObject* Instance, EPropertyFilter Filter);
	static TSharedPtr<FJsonValue> WriteValue(const FProperty* Property, const void* DataPtr);

	static bool IsDefault(const FProperty* Property, TSharedPtr<FJsonValue> JsonValue, const void* NativeValue);

	bool LoadFromFile(FStringView FilePath);
	bool SaveToFile(FStringView FilePath) const;

	void SetDirty();

private:
	TSharedPtr<UE::FJsonConfig> JsonConfig;
	TSharedPtr<FEditorConfig> ParentConfig;
		
	FOnEditorConfigDirtied EditorConfigDirtiedEvent;
	bool Dirty { false };
};

template <typename T>
bool FEditorConfig::TryGetStruct(FStringView Key, T& OutValue, EPropertyFilter Filter) const
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

	const UStruct* Struct = T::StaticStruct();
	ReadStruct(StructData, Struct, &OutValue, nullptr, Filter);

	return true;
}

template <typename T>
bool FEditorConfig::TryGetUObject(FStringView Key, T& OutValue, EPropertyFilter Filter) const
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

	const UClass* Class = T::StaticClass();
	ReadUObject(UObjectData, Class, &OutValue, Filter);

	return true;
}

template <typename T>
bool FEditorConfig::TryGetRootStruct(T& OutValue, EPropertyFilter Filter) const
{
	return TryGetRootStruct(T::StaticStruct(), &OutValue, Filter);
}

template <typename T>
bool FEditorConfig::TryGetRootUObject(T& OutValue, EPropertyFilter Filter) const
{
	static_assert(TIsDerivedFrom<T, UObject>::Value, "Type is not derived from UObject.");

	return TryGetRootUObject(T::StaticClass(), &OutValue, Filter);
}

template <typename T>
void FEditorConfig::SetStruct(FStringView Key, const T& InValue, EPropertyFilter Filter)
{
	if (!IsValid())
	{
		return;
	}

	TSharedPtr<FJsonObject> JsonObject = WriteStruct(T::StaticStruct(), &InValue, Filter);
	JsonConfig->SetJsonObject(UE::FJsonPath(Key), JsonObject);

	SetDirty();
}

template <typename T>
void FEditorConfig::SetUObject(FStringView Key, const T& InValue, EPropertyFilter Filter)
{
	static_assert(TIsDerivedFrom<T, UObject>::Value, "Type is not derived from UObject.");

	if (!IsValid())
	{
		return;
	}
	
	TSharedPtr<FJsonObject> JsonObject = WriteUObject(T::StaticClass(), &InValue);
	JsonConfig->SetJsonObject(UE::FJsonPath(Key), JsonObject);
		
	SetDirty();
}

template <typename T>
void FEditorConfig::SetRootStruct(const T& InValue, EPropertyFilter Filter)
{
	SetRootStruct(T::StaticStruct(), &InValue, Filter);
}

template <typename T>
void FEditorConfig::SetRootUObject(const T& InValue, EPropertyFilter Filter)
{
	static_assert(TIsDerivedFrom<T, UObject>::Value, "Type is not derived from UObject.");

	SetRootUObject(T::StaticClass(), &InValue, Filter);
}
