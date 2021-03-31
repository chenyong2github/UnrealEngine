// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/ManagedArrayCollection.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "Templates/UnrealTemplate.h"
#include "Chaos/ChaosArchive.h"

DEFINE_LOG_CATEGORY_STATIC(FManagedArrayCollectionLogging, NoLogging, All);

int8 FManagedArrayCollection::Invalid = INDEX_NONE;


FManagedArrayCollection::FManagedArrayCollection()
{
	Version = 5;
}

static const FName GuidName("GUID");

void FManagedArrayCollection::AddGroup(FName Group)
{
	ensure(!GroupInfo.Contains(Group));
	FGroupInfo info{
		0
	};
	GroupInfo.Add(Group, info);

	//Every group has to have a GUID attribute
	AddAttribute<FGuid>(GuidName, Group);
}

void FManagedArrayCollection::RemoveElements(const FName& Group, const TArray<int32>& SortedDeletionList, FProcessingParameters Params)
{
	if (SortedDeletionList.Num())
	{
		int32 GroupSize = NumElements(Group);
		int32 DelListNum = SortedDeletionList.Num();
		GeometryCollectionAlgo::ValidateSortedList(SortedDeletionList, GroupSize);
		ensure(GroupSize >= DelListNum);

		TArray<int32> Offsets;
		GeometryCollectionAlgo::BuildIncrementMask(SortedDeletionList, GroupSize, Offsets);

		for (const TTuple<FKeyType, FValueType>& Entry : Map)
		{
			//
			// Reindex attributes dependent on the group being resized
			//
			if (Entry.Value.GroupIndexDependency == Group && Params.bReindexDependentAttibutes)
			{
				Entry.Value.Value->Reindex(Offsets, GroupSize - DelListNum, SortedDeletionList);
			}

			//
			//  Resize the array and clobber deletion indices
			//
			if (Entry.Key.Get<1>() == Group)
			{
				Entry.Value.Value->RemoveElements(SortedDeletionList);
			}

		}
		GroupInfo[Group].Size -= DelListNum;
	}
}


TArray<FName> FManagedArrayCollection::GroupNames() const
{
	TArray<FName> keys;
	if (GroupInfo.Num())
	{
		GroupInfo.GetKeys(keys);
	}
	return keys;
}

bool FManagedArrayCollection::HasAttribute(FName Name, FName Group) const
{
	bool bReturnValue = false;
	for (const TTuple<FKeyType, FValueType>& Entry : Map)
	{
		if (Entry.Key.Get<0>() == Name && Entry.Key.Get<1>() == Group)
		{
			bReturnValue = true;
			break;
		}
	}
	return bReturnValue;
}

TArray<FName> FManagedArrayCollection::AttributeNames(FName Group) const
{
	TArray<FName> AttributeNames;
	for (const TTuple<FKeyType, FValueType>& Entry : Map)
	{
		if (Entry.Key.Get<1>() == Group)
		{
			AttributeNames.Add(Entry.Key.Get<0>());
		}
	}
	return AttributeNames;
}

int32 FManagedArrayCollection::NumElements(FName Group) const
{
	int32 Num = 0;
	if (GroupInfo.Contains(Group))
	{
		Num = GroupInfo[Group].Size;
	}
	return Num;
}

/** Should be called whenever new elements are added. Generates guids for new entries */
void FManagedArrayCollection::GenerateGuids(FName Group, int32 StartIdx)
{
	TManagedArray<FGuid>& Guids = GetAttribute<FGuid>(GuidName, Group);

	// we don't actually rely on this at the moment and generating the guids is very expensive.
	// we don't need these at runtime in any case, so if we need in the editor later, make sure this is in-editor only
	if (GIsEditor)
	{
		for (int32 Idx = StartIdx; Idx < Guids.Num(); ++Idx)
		{
			Guids[Idx] = FGuid::NewGuid();
		}
	}
}

int32 FManagedArrayCollection::AddElements(int32 NumberElements, FName Group)
{
	int32 StartSize = 0;
	if (!GroupInfo.Contains(Group))
	{
		AddGroup(Group);
	}

	StartSize = GroupInfo[Group].Size;
	for (TTuple<FKeyType, FValueType>& Entry : Map)
	{
		if (Entry.Key.Get<1>() == Group)
		{
			Entry.Value.Value->Resize(StartSize + NumberElements);
		}
	}
	GenerateGuids(Group, StartSize);
	GroupInfo[Group].Size += NumberElements;

	SetDefaults(Group, StartSize, NumberElements);

	return StartSize;
}

void FManagedArrayCollection::RemoveAttribute(FName Name, FName Group)
{
	for (const TTuple<FKeyType, FValueType>& Entry : Map)
	{
		if (Entry.Key.Get<0>() == Name && Entry.Key.Get<1>() == Group)
		{
			Map.Remove(Entry.Key);
			return;
		}
	}
}

void FManagedArrayCollection::RemoveGroup(FName Group)
{
	TArray<FName> DelList;
	for (TTuple<FKeyType, FValueType>& Entry : Map)
	{
		if (Entry.Key.Get<1>() == Group)
		{
			DelList.Add(Entry.Key.Get<0>());
		}
		if (Entry.Value.GroupIndexDependency == Group)
		{
			Entry.Value.GroupIndexDependency = "";
		}
	}
	for (const FName& AttrName : DelList)
	{
		Map.Remove(FManagedArrayCollection::MakeMapKey(AttrName, Group));
	}

	GroupInfo.Remove(Group);
}

void FManagedArrayCollection::Resize(int32 Size, FName Group)
{
	ensure(HasGroup(Group));
	const int32 CurSize = NumElements(Group);
	if (CurSize == Size)
	{
		return;
	}

	ensureMsgf(Size > NumElements(Group), TEXT("Use RemoveElements to shrink a group."));
	const int32 StartSize = GroupInfo[Group].Size;
	for (TTuple<FKeyType, FValueType>& Entry : Map)
	{
		if (Entry.Key.Get<1>() == Group)
		{
			Entry.Value.Value->Resize(Size);
		}
	}
	GenerateGuids(Group, StartSize);
	GroupInfo[Group].Size = Size;
}

void FManagedArrayCollection::Reserve(int32 Size, FName Group)
{
	ensure(HasGroup(Group));
	const int32 CurSize = NumElements(Group);
	if (CurSize >= Size)
	{
		return;
	}

	const int32 StartSize = GroupInfo[Group].Size;
	for (TTuple<FKeyType, FValueType>& Entry : Map)
	{
		if (Entry.Key.Get<1>() == Group)
		{
			Entry.Value.Value->Reserve(Size);
		}
	}
}

void FManagedArrayCollection::EmptyGroup(FName Group)
{
	ensure(HasGroup(Group));

	for (TTuple<FKeyType, FValueType>& Entry : Map)
	{
		if (Entry.Key.Get<1>() == Group)
		{
			Entry.Value.Value->Empty();
		}
	}

	GroupInfo[Group].Size = 0;
}

void FManagedArrayCollection::ReorderElements(FName Group, const TArray<int32>& NewOrder)
{
	const int32 GroupSize = GroupInfo[Group].Size;
	check(GroupSize == NewOrder.Num());

	TArray<int32> InverseNewOrder;
	InverseNewOrder.Init(-1, GroupSize);
	for (int32 Idx = 0; Idx < GroupSize; ++Idx)
	{
		InverseNewOrder[NewOrder[Idx]] = Idx;
	}

	for (TTuple<FKeyType, FValueType>& Entry : Map)
	{
		// Reindex attributes dependent on the group being reordered
		if (Entry.Value.GroupIndexDependency == Group)
		{
			Entry.Value.Value->ReindexFromLookup(InverseNewOrder);
		}

		if (Entry.Key.Get<1>() == Group)
		{
			Entry.Value.Value->Reorder(NewOrder);
		}
	}
}

void FManagedArrayCollection::SetDependency(FName Name, FName Group, FName DependencyGroup)
{
	ensure(HasAttribute(Name, Group));
	if (ensure(!HasCycle(Group, DependencyGroup)))
	{
		FKeyType Key = FManagedArrayCollection::MakeMapKey(Name, Group);
		Map[Key].GroupIndexDependency = DependencyGroup;
	}
}

void FManagedArrayCollection::RemoveDependencyFor(FName Group)
{
	ensure(HasGroup(Group));
	for (TTuple<FKeyType, FValueType>& Entry : Map)
	{
		if (Entry.Value.GroupIndexDependency == Group)
		{
			Entry.Value.GroupIndexDependency = "";
		}
	}
}

void FManagedArrayCollection::SyncGroupSizeFrom(const FManagedArrayCollection& InCollection, FName Group)
{
	if (!HasGroup(Group))
	{
		AddGroup(Group);
	}

	Resize(InCollection.GroupInfo[Group].Size, Group);
}

void FManagedArrayCollection::CopyMatchingAttributesFrom(
	const FManagedArrayCollection& InCollection,
	const TMap<FName, TSet<FName>>* SkipList)
{
	for (const auto& Pair : InCollection.GroupInfo)
	{
		SyncGroupSizeFrom(InCollection, Pair.Key);
	}
	for (TTuple<FKeyType, FValueType>& Entry : Map)
	{
		if (SkipList)
		{
			if (const TSet<FName>* Attrs = SkipList->Find(Entry.Key.Get<1>()))
			{
				if (Attrs->Contains(Entry.Key.Get<0>()))
				{
					continue;
				}
			}
		}
		if (InCollection.HasAttribute(Entry.Key.Get<0>(), Entry.Key.Get<1>()))
		{
			const FValueType& OriginalValue = InCollection.Map[Entry.Key];
			const FValueType& DestValue = Map[Entry.Key];

			// If we don't have a type match don't attempt the copy.
			if(OriginalValue.ArrayType == DestValue.ArrayType)
			{
				CopyAttribute(InCollection, Entry.Key.Get<0>(), Entry.Key.Get<1>());
			}
		}
	}

}

void FManagedArrayCollection::CopyAttribute(const FManagedArrayCollection& InCollection, FName Name, FName Group)
{
	SyncGroupSizeFrom(InCollection, Group);
	FKeyType Key = FManagedArrayCollection::MakeMapKey(Name, Group);

	const FValueType& OriginalValue = InCollection.Map[Key];
	const FValueType& DestValue = Map[Key];	//todo(ocohen): API assumes an AddAttribute is called before copy is done. It'd be nice to handle the case where AddAttribute was not done first
	check(OriginalValue.ArrayType == DestValue.ArrayType);
	DestValue.Value->Init(*OriginalValue.Value);
}

FName FManagedArrayCollection::GetDependency(FName SearchGroup)
{
	FName GroupIndexDependency = "";

	for (const TTuple<FKeyType, FValueType>& Entry : Map)
	{
		if (Entry.Key.Get<1>() == SearchGroup)
		{
			GroupIndexDependency = Entry.Value.GroupIndexDependency;
		}
	}

	return GroupIndexDependency;
}

bool FManagedArrayCollection::HasCycle(FName NewGroup, FName DependencyGroup)
{
	if (!DependencyGroup.IsNone())
	{
		// The system relies adding a dependency on it own group in order to run the reinding methods
		// this is why we don't include the case if (NewGroup == DependencyGroup) return true;

		while (!(DependencyGroup = GetDependency(DependencyGroup)).IsNone())
		{
			// check if we are looping back to the group we are testing against
			if (DependencyGroup == NewGroup)
			{
				return true;
			}
		}

	}

	return false;
}


#include <sstream> 
#include <string>
FString FManagedArrayCollection::ToString() const
{
	FString Buffer("");
	for (FName GroupName : GroupNames())
	{
		Buffer += GroupName.ToString() + "\n";
		for (FName AttributeName : AttributeNames(GroupName))
		{
			FKeyType Key = FManagedArrayCollection::MakeMapKey(AttributeName, GroupName);
			const FValueType& Value = Map[Key];

			const void* PointerAddress = static_cast<const void*>(Value.Value);
			std::stringstream AddressStream;
			AddressStream << PointerAddress;

			Buffer += GroupName.ToString() + ":" + AttributeName.ToString() + " [" + FString(AddressStream.str().c_str()) + "]\n";
		}
	}
	return Buffer;
}


void FManagedArrayCollection::Serialize(Chaos::FChaosArchive& Ar)
{
	if (Ar.IsSaving()) Version = 5;
	Ar << Version;

	if (Ar.IsLoading())
	{
		//We can't serialize entire tmap in place because we may have new groups. todo(ocohen): baked data should be simpler since all entries exist
		TMap< FName, FGroupInfo> TmpGroupInfo;
		Ar << TmpGroupInfo;

		for (TTuple<FName, FGroupInfo>& Group : TmpGroupInfo)
		{
			GroupInfo.Add(Group);
		}

		//We can't serialize entire tmap in place because some entries may have changed types or memory ownership (internal vs external).
		//todo(ocohen): baked data should be simpler since all entries are guaranteed to exist
		TMap< FKeyType, FValueType> TmpMap;
		Ar << TmpMap;

		for (TTuple<FKeyType, FValueType>& Pair : TmpMap)
		{
			if (FValueType* Existing = Map.Find(Pair.Key))
			{
				if (ensureMsgf(Existing->ArrayType == Pair.Value.ArrayType, TEXT("Type change not supported. Ignoring serialized data")))
				{
					Existing->Value->ExchangeArrays(*Pair.Value.Value);	//if there is already an entry do an exchange. This way external arrays get correct serialization
					//question: should we validate if group dependency has changed in some invalid way?
				}
			}
			else
			{
				//todo(ocohen): how do we remove old values? Maybe have an unused attribute concept
				//no existing entry so it is owned by the map
				Map.Add(Pair.Key, MoveTemp(Pair.Value));
			}
		}

#if WITH_EDITOR
		//it's possible new entries have been added but are not in old content. Resize these.
		for (TTuple<FKeyType, FValueType>& Pair : Map)
		{
			const int32 GroupSize = GroupInfo[Pair.Key.Get<1>()].Size;
			if (GroupSize != Pair.Value.Value->Num())
			{
				Pair.Value.Value->Resize(GroupSize);
			}
		}
		if (Version < 4)
		{
			//old content has no guids
			for (TTuple<FName, FGroupInfo>& Pair : GroupInfo)
			{
				GenerateGuids(Pair.Key, 0);
			}
		}
#endif
	}
	else
	{
		Ar << GroupInfo;
		Ar << Map;
	}
}

FArchive& operator<<(FArchive& Ar, FManagedArrayCollection::FGroupInfo& GroupInfo)
{
	int Version = 4;
	Ar << Version;
	Ar << GroupInfo.Size;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FManagedArrayCollection::FValueType& ValueIn)
{
	int Version = 4;	//todo: version per entry is really bloated
	Ar << Version;

	int ArrayTypeAsInt = static_cast<int>(ValueIn.ArrayType);
	Ar << ArrayTypeAsInt;
	ValueIn.ArrayType = static_cast<FManagedArrayCollection::EArrayType>(ArrayTypeAsInt);

	if (Version < 4)
	{
		int ArrayScopeAsInt;
		Ar << ArrayScopeAsInt;	//assume all serialized old content was for rest collection
	}

	if (Version >= 2)
	{
		Ar << ValueIn.GroupIndexDependency;
		Ar << ValueIn.Saved;	//question: should we be saving if Saved is false?
	}

	if (ValueIn.Value == nullptr)
	{
		ValueIn.Value = NewManagedTypedArray(ValueIn.ArrayType);
	}

	if (ValueIn.Saved)
	{
		//todo(ocohen): need a better way to enforce this
		ValueIn.Value->Serialize(static_cast<Chaos::FChaosArchive&>(Ar));
	}

	return Ar;
}