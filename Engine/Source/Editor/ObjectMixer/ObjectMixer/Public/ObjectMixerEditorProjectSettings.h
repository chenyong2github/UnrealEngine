// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Algo/Find.h"

#include "ObjectMixerEditorProjectSettings.generated.h"

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

UCLASS(config = Engine, defaultconfig)
class OBJECTMIXEREDITOR_API UObjectMixerEditorProjectSettings : public UObject
{
	GENERATED_BODY()
public:
	
	UObjectMixerEditorProjectSettings(const FObjectInitializer& ObjectInitializer)
	{}

	/**
	 * If false, a new object will be created every time the filter object is accessed.
	 */
	UPROPERTY(Config, EditAnywhere, Category="Object Mixer")
	bool bExpandTreeViewItemsByDefault = true;

	UPROPERTY(Config)
	TSet<FObjectMixerSerializationData> SerializedData;

	FObjectMixerSerializationData* FindSerializationDataByFilterClassName(const FName& FilterClassName)
	{
		if (FObjectMixerSerializationData* Match = Algo::FindByPredicate(SerializedData,
			[FilterClassName](const FObjectMixerSerializationData& Comparator)
			{
				return Comparator.FilterClassName.IsEqual(FilterClassName);
			}))
		{
			return Match;
		}

		return nullptr;
	}

	void AddObjectsToCategory(const FName& FilterClassName, const FName& CategoryName, const TSet<FSoftObjectPath>& ObjectsToAdd)
	{
		FObjectMixerSerializationData* Data = FindSerializationDataByFilterClassName(FilterClassName);

		if (!Data)
		{
			SerializedData.Add({FilterClassName});
			Data = FindSerializationDataByFilterClassName(FilterClassName);
		}
		
		if (Data)
		{
			if (FObjectMixerCategoryObjectSet* Match = Algo::FindByPredicate(Data->SerializedCategories,
				[CategoryName](const FObjectMixerCategoryObjectSet& Comparator)
				{
					return Comparator.CategoryName.IsEqual(CategoryName);
				}))
			{
				(*Match).CategoryObjects.Append(ObjectsToAdd);
			}
			else
			{
				Data->SerializedCategories.Add({CategoryName, ObjectsToAdd});
			}

			SaveConfig();
		}
	}

	void RemoveObjectsFromCategory(const FName& FilterClassName, const FName& CategoryName, const TSet<FSoftObjectPath>& ObjectsToRemove)
	{
		if (FObjectMixerSerializationData* Data = FindSerializationDataByFilterClassName(FilterClassName))
		{
			if (FObjectMixerCategoryObjectSet* Match = Algo::FindByPredicate(Data->SerializedCategories,
				[CategoryName](const FObjectMixerCategoryObjectSet& Comparator)
				{
					return Comparator.CategoryName.IsEqual(CategoryName);
				}))
			{
				for (const FSoftObjectPath& ObjectPath : ObjectsToRemove)
				{
					(*Match).CategoryObjects.Remove(ObjectPath);
				}
				
				SaveConfig();
			}
		}
	}

	void RemoveCategory(const FName& FilterClassName, const FName& CategoryName)
	{
		if (FObjectMixerSerializationData* Data = FindSerializationDataByFilterClassName(FilterClassName))
		{
			if (FObjectMixerCategoryObjectSet* Match = Algo::FindByPredicate(Data->SerializedCategories,
				[CategoryName](const FObjectMixerCategoryObjectSet& Comparator)
				{
					return Comparator.CategoryName.IsEqual(CategoryName);
				}))
			{
				(*Match).CategoryObjects.Empty();

				Data->SerializedCategories.Remove(*Match);

				SaveConfig();
			}
		}
	}

	bool IsObjectInCategory(const FName& FilterClassName, const FName& CategoryName, const FSoftObjectPath& InObject)
	{
		if (FObjectMixerSerializationData* Data = FindSerializationDataByFilterClassName(FilterClassName))
		{
			if (const FObjectMixerCategoryObjectSet* Match = Algo::FindByPredicate(Data->SerializedCategories,
				[CategoryName](const FObjectMixerCategoryObjectSet& Comparator)
				{
					return Comparator.CategoryName.IsEqual(CategoryName);
				}))
			{
				return (*Match).CategoryObjects.Contains(InObject);
			}
		}

		return false;
	}

	TSet<FName> GetCategoriesForObject(const FName& FilterClassName, const FSoftObjectPath& InObject)
	{
		if (FObjectMixerSerializationData* Data = FindSerializationDataByFilterClassName(FilterClassName))
		{
			TSet<FName> CategoriesWithObject;

			for (const FObjectMixerCategoryObjectSet& SerializedCategory : Data->SerializedCategories)
			{
				if (IsObjectInCategory(FilterClassName, SerializedCategory.CategoryName, InObject))
				{
					CategoriesWithObject.Add(SerializedCategory.CategoryName);
				}
			}

			return CategoriesWithObject;
		}

		return {};
	}

	TSet<FName> GetAllCategories(const FName& FilterClassName)
	{
		if (FObjectMixerSerializationData* Data = FindSerializationDataByFilterClassName(FilterClassName))
		{
			TSet<FName> Categories;

			for (const FObjectMixerCategoryObjectSet& SerializedCategory : Data->SerializedCategories)
			{
				Categories.Add(SerializedCategory.CategoryName);
			}

			return Categories;
		}

		return {};
	}
};
