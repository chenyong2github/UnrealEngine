// Copyright Epic Games, Inc. All Rights Reserved.

#include "ObjectMixerEditorSerializedData.h"

#include "Algo/Find.h"
#include "Algo/RemoveIf.h"
#include "ScopedTransaction.h"
#include "Algo/IndexOf.h"

#define LOCTEXT_NAMESPACE "ObjectMixerEditor"

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

void UObjectMixerEditorSerializedData::AddObjectsToCollection(const FName& FilterClassName, const FName& CollectionName, const TSet<FSoftObjectPath>& ObjectsToAdd)
{
	FScopedTransaction AddObjectsToCollectionTransaction(LOCTEXT("AddObjectsToCollectionTransaction","Add Objects To Collection"));

	Modify();
	
	FObjectMixerSerializationData* Data = FindSerializationDataByFilterClassName(FilterClassName);

	if (!Data)
	{
		SerializedData.Add({FilterClassName});
		Data = FindSerializationDataByFilterClassName(FilterClassName);
	}
	
	if (Data)
	{
		if (FObjectMixerCollectionObjectSet* Match = Algo::FindByPredicate(Data->SerializedCollection,
			[CollectionName](const FObjectMixerCollectionObjectSet& Comparator)
			{
				return Comparator.CollectionName.IsEqual(CollectionName);
			}))
		{
			(*Match).CollectionObjects.Append(ObjectsToAdd);
		}
		else
		{
			Data->SerializedCollection.Add({CollectionName, ObjectsToAdd});
		}

		SaveConfig();
	}
}

void UObjectMixerEditorSerializedData::RemoveObjectsFromCollection(const FName& FilterClassName, const FName& CollectionName, const TSet<FSoftObjectPath>& ObjectsToRemove)
{
	if (FObjectMixerSerializationData* Data = FindSerializationDataByFilterClassName(FilterClassName))
	{
		FScopedTransaction RemoveCollectionTransaction(LOCTEXT("RemoveCollectionTransaction","Remove Collection"));
	
		Modify();
		
		Data->SerializedCollection.SetNum(Algo::StableRemoveIf(Data->SerializedCollection,
			[CollectionName](const FObjectMixerCollectionObjectSet& Comparator)
			{
				return Comparator.CollectionName.IsEqual(CollectionName);
			}));
		
		SaveConfig();
	}
}

void UObjectMixerEditorSerializedData::RemoveCollection(const FName& FilterClassName, const FName& CollectionName)
{	
	if (FObjectMixerSerializationData* Data = FindSerializationDataByFilterClassName(FilterClassName))
	{
		FScopedTransaction RemoveCollectionTransaction(LOCTEXT("RemoveCollectionTransaction","Remove Collection"));
	
		Modify();
		
		Data->SerializedCollection.SetNum(Algo::StableRemoveIf(Data->SerializedCollection,
			[CollectionName](const FObjectMixerCollectionObjectSet& Comparator)
			{
				return Comparator.CollectionName.IsEqual(CollectionName);
			}));
		
		SaveConfig();
	}
}

void UObjectMixerEditorSerializedData::ReorderCollection(const FName& FilterClassName,
	const FName& CollectionToMoveName, const FName& CollectionInsertBeforeName)
{
	if (FObjectMixerSerializationData* Data = FindSerializationDataByFilterClassName(FilterClassName))
	{
		FObjectMixerCollectionObjectSet MatchCopy;
		bool bFoundMatch = false;
		if (const FObjectMixerCollectionObjectSet* Match = Algo::FindByPredicate(Data->SerializedCollection,
			[CollectionToMoveName](const FObjectMixerCollectionObjectSet& Comparator)
			{
				return Comparator.CollectionName.IsEqual(CollectionToMoveName);
			}))
		{
			MatchCopy = (*Match);
			bFoundMatch = true;
		}

		if (bFoundMatch)
		{
			FScopedTransaction RemoveCollectionTransaction(LOCTEXT("RemoveCollectionTransaction","Remove Collection"));
	
			Modify();
			
			RemoveCollection(FilterClassName, CollectionToMoveName);

			if (CollectionInsertBeforeName == "All") // Move MatchCopy to the end
			{
				Data->SerializedCollection.Add(MatchCopy);
			}
			else
			{
				const int32 IndexOfCollectionInsertBefore = Algo::IndexOfByPredicate(Data->SerializedCollection,
					[CollectionInsertBeforeName](const FObjectMixerCollectionObjectSet& Comparator)
					{
						return Comparator.CollectionName.IsEqual(CollectionInsertBeforeName);
					});

				Data->SerializedCollection.Insert(MatchCopy, IndexOfCollectionInsertBefore);
			}
		
			SaveConfig();
		}
	}
}

bool UObjectMixerEditorSerializedData::IsObjectInCollection(const FName& FilterClassName, const FName& CollectionName, const FSoftObjectPath& InObject)
{
	if (FObjectMixerSerializationData* Data = FindSerializationDataByFilterClassName(FilterClassName))
	{
		if (const FObjectMixerCollectionObjectSet* Match = Algo::FindByPredicate(Data->SerializedCollection,
			[CollectionName](const FObjectMixerCollectionObjectSet& Comparator)
			{
				return Comparator.CollectionName.IsEqual(CollectionName);
			}))
		{
			return (*Match).CollectionObjects.Contains(InObject);
		}
	}

	return false;
}

TSet<FName> UObjectMixerEditorSerializedData::GetCollectionsForObject(const FName& FilterClassName, const FSoftObjectPath& InObject)
{
	if (FObjectMixerSerializationData* Data = FindSerializationDataByFilterClassName(FilterClassName))
	{
		TSet<FName> CollectionsWithObject;

		for (const FObjectMixerCollectionObjectSet& CollectionObjectSet : Data->SerializedCollection)
		{
			if (IsObjectInCollection(FilterClassName, CollectionObjectSet.CollectionName, InObject))
			{
				CollectionsWithObject.Add(CollectionObjectSet.CollectionName);
			}
		}

		return CollectionsWithObject;
	}

	return {};
}

TArray<FName> UObjectMixerEditorSerializedData::GetAllCollectionNames(const FName& FilterClassName)
{
	if (FObjectMixerSerializationData* Data = FindSerializationDataByFilterClassName(FilterClassName))
	{
		TArray<FName> Collections;

		for (const FObjectMixerCollectionObjectSet& CollectionObjectSet : Data->SerializedCollection)
		{
			Collections.Add(CollectionObjectSet.CollectionName);
		}

		return Collections;
	}

	return {};
}

#undef LOCTEXT_NAMESPACE
