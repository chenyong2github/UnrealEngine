// Copyright Epic Games, Inc. All Rights Reserved.

#include "ObjectMixerEditorSerializedData.h"

#include "Algo/Find.h"

FObjectMixerSerializationData* UObjectMixerEditorSerializedData::FindSerializationDataByFilterClassName(const FName& FilterClassName)
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

void UObjectMixerEditorSerializedData::AddObjectsToCategory(const FName& FilterClassName, const FName& CategoryName, const TSet<FSoftObjectPath>& ObjectsToAdd)
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

void UObjectMixerEditorSerializedData::RemoveObjectsFromCategory(const FName& FilterClassName, const FName& CategoryName, const TSet<FSoftObjectPath>& ObjectsToRemove)
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

void UObjectMixerEditorSerializedData::RemoveCategory(const FName& FilterClassName, const FName& CategoryName)
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

bool UObjectMixerEditorSerializedData::IsObjectInCategory(const FName& FilterClassName, const FName& CategoryName, const FSoftObjectPath& InObject)
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

TSet<FName> UObjectMixerEditorSerializedData::GetCategoriesForObject(const FName& FilterClassName, const FSoftObjectPath& InObject)
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

TSet<FName> UObjectMixerEditorSerializedData::GetAllCategories(const FName& FilterClassName)
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
