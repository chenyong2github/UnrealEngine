// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ObjectMixerEditorSerializedData.generated.h"

USTRUCT()
struct FObjectMixerCategoryObjectSet
{
	GENERATED_BODY()
	
	UPROPERTY()
	FName CategoryName = NAME_None;

	UPROPERTY()
	TSet<FSoftObjectPath> CategoryObjects = {};

	bool operator==(const FObjectMixerCategoryObjectSet& Other) const
	{
		return CategoryName.IsEqual(Other.CategoryName);
	}

	friend uint32 GetTypeHash (const FObjectMixerCategoryObjectSet& Other)
	{
		return GetTypeHash(Other.CategoryName);
	}
};

USTRUCT()
struct FObjectMixerSerializationData
{
	GENERATED_BODY()
	
	UPROPERTY()
	FName FilterClassName = NAME_None;

	UPROPERTY()
	TSet<FObjectMixerCategoryObjectSet> SerializedCategories = {};

	bool operator==(const FObjectMixerSerializationData& Other) const
	{
		return FilterClassName.IsEqual(Other.FilterClassName);
	}

	friend uint32 GetTypeHash (const FObjectMixerSerializationData& Other)
	{
		return GetTypeHash(Other.FilterClassName);
	}
};

UCLASS(config = ObjectMixerSerializedData)
class OBJECTMIXEREDITOR_API UObjectMixerEditorSerializedData : public UObject
{
	GENERATED_BODY()
public:
	
	UObjectMixerEditorSerializedData(const FObjectInitializer& ObjectInitializer)
	{}
	
	UPROPERTY(Config)
	TSet<FObjectMixerSerializationData> SerializedData;

	FObjectMixerSerializationData* FindSerializationDataByFilterClassName(const FName& FilterClassName);

	void AddObjectsToCategory(const FName& FilterClassName, const FName& CategoryName, const TSet<FSoftObjectPath>& ObjectsToAdd);

	void RemoveObjectsFromCategory(const FName& FilterClassName, const FName& CategoryName, const TSet<FSoftObjectPath>& ObjectsToRemove);

	void RemoveCategory(const FName& FilterClassName, const FName& CategoryName);

	bool IsObjectInCategory(const FName& FilterClassName, const FName& CategoryName, const FSoftObjectPath& InObject);

	TSet<FName> GetCategoriesForObject(const FName& FilterClassName, const FSoftObjectPath& InObject);

	TSet<FName> GetAllCategories(const FName& FilterClassName);
};
