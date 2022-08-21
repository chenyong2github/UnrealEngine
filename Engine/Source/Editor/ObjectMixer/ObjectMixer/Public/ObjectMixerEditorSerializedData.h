// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ObjectMixerEditorSerializedData.generated.h"

USTRUCT()
struct FObjectMixerCollectionObjectSet
{
	GENERATED_BODY()
	
	UPROPERTY()
	FName CollectionName = NAME_None;

	UPROPERTY()
	TSet<FSoftObjectPath> CollectionObjects = {};

	bool operator==(const FObjectMixerCollectionObjectSet& Other) const
	{
		return CollectionName.IsEqual(Other.CollectionName);
	}

	friend uint32 GetTypeHash (const FObjectMixerCollectionObjectSet& Other)
	{
		return GetTypeHash(Other.CollectionName);
	}
};

USTRUCT()
struct FObjectMixerSerializationData
{
	GENERATED_BODY()
	
	UPROPERTY()
	FName FilterClassName = NAME_None;

	UPROPERTY()
	TSet<FObjectMixerCollectionObjectSet> SerializedCollection = {};

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

	void AddObjectsToCollection(const FName& FilterClassName, const FName& CollectionName, const TSet<FSoftObjectPath>& ObjectsToAdd);

	void RemoveObjectsFromCollection(const FName& FilterClassName, const FName& CollectionName, const TSet<FSoftObjectPath>& ObjectsToRemove);

	void RemoveCollection(const FName& FilterClassName, const FName& CollectionName);

	bool IsObjectInCollection(const FName& FilterClassName, const FName& CollectionName, const FSoftObjectPath& InObject);

	TSet<FName> GetCollectionsForObject(const FName& FilterClassName, const FSoftObjectPath& InObject);

	TSet<FName> GetAllCollections(const FName& FilterClassName);
};
