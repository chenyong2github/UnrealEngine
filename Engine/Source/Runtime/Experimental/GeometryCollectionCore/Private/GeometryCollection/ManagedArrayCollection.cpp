// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/ManagedArrayCollection.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "Templates/UnrealTemplate.h"
#include "Chaos/ChaosArchive.h"

DEFINE_LOG_CATEGORY_STATIC(FManagedArrayCollectionLogging, NoLogging, All);

int8 FManagedArrayCollection::Invalid = -1;


FManagedArrayCollection::FManagedArrayCollection()
{
}

static const FName GuidName("GUID");

void FManagedArrayCollection::AddGroup(FName Group)
{
	ensure(!GroupInfo.Contains(Group));
	FGroupInfo info {
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
	for (const TTuple<FKeyType,FValueType>& Entry : Map)
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
	for (const FName & AttrName : DelList)
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

	ensureMsgf(Size > NumElements(Group),TEXT("Use RemoveElements to shrink a group."));
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

void FManagedArrayCollection::ReorderElements(FName Group, const TArray<int32>& NewOrder)
{
	const int32 GroupSize = GroupInfo[Group].Size;
	check(GroupSize == NewOrder.Num());

	for (TTuple<FKeyType, FValueType>& Entry : Map)
	{
		// Reindex attributes dependent on the group being reordered
		if (Entry.Value.GroupIndexDependency == Group)
		{
			Entry.Value.Value->ReindexFromLookup(NewOrder);
		}

		if (Entry.Key.Get<1>() == Group)
		{
			Entry.Value.Value->Reorder(NewOrder);
		}
	}
}

#if 0	//not needed until we support per instance serialization
void FManagedArrayCollection::SwapElements(int32 Index1, int32 Index2, FName Group)
{
	//todo(ocohen): handle dependent groups
	for (TTuple<FKeyType, FValueType>& Entry : Map)
	{
		if (Entry.Key.Get<1>() == Group)
		{
			Entry.Value.Value->Swap(Index1, Index2);
		}
	}
}
#endif

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

void FManagedArrayCollection::SyncGroupSizeAndOrder(const FManagedArrayCollection& MasterCollection, FName Group)
{
	if (!HasGroup(Group))
	{
		AddGroup(Group);
	}

	//For now we ignore order and just sync size. Ordering is needed for saving out per instance changes. Will add this soon
	const int32 GroupSize = MasterCollection.GroupInfo[Group].Size;
	Resize(GroupSize, Group);

#if 0
	//We want to lock the group and ensure that the master collection's guids match ours in order and size.
	//Existing entries with guids must be preserved and simply re-ordered
	//Entries with mismatching guids will be deleted
	//New entries will be created to ensure the size is the same.

	const TManagedArray<FGuid>& MasterGuids = MasterCollection.GetAttribute<FGuid>(GuidName, Group);
	TManagedArray<FGuid>& SlaveGuids = GetAttribute<FGuid>(GuidName, Group);
	
	//remove guids that are not in the master list
	TArray<int32> SortedEntriesToDelete;
	SortedEntriesToDelete.Reserve(SlaveGuids.Num());
	for (int32 Idx = 0; Idx < SlaveGuids.Num(); ++Idx)
	{
		if (!MasterGuids.Contains(SlaveGuids[Idx]))	//comment: set may help, but it doesn't take TManagedArray directly
		{
			SortedEntriesToDelete.Add(Idx);
		}
	}
	RemoveElements(Group, SortedEntriesToDelete);
	//Slave guids are now a subset of the master guids, but they can be out of order. So we must re-order and add new guids
	for (int32 Idx = 0; Idx < MasterGuids.Num(); ++Idx)
	{
		int32 FoundSlaveIdx = -1;
		{
			for (int32 SlaveIdx = 0; SlaveIdx < SlaveGuids.Num(); ++SlaveIdx)
			{
				if (MasterGuids[Idx] == SlaveGuids[SlaveIdx])
				{
					FoundSlaveIdx = SlaveIdx;
					break;
				}
			}
		}

		if (FoundSlaveIdx == -1)
		{
			//no existing entry, create a new one and set its guid
			FoundSlaveIdx = NumElements(Group);
			AddElements(1, Group);
			SlaveGuids[FoundSlaveIdx] = MasterGuids[Idx];
		}

		if(FoundSlaveIdx != Idx)
		{
			//entry exists, just needs to be re-ordered
			SwapElements(FoundSlaveIdx, Idx, Group);
		}
	}
#endif
}

void FManagedArrayCollection::SyncAllGroups(const FManagedArrayCollection& MasterCollection)
{
	for (const auto& Pair : MasterCollection.GroupInfo)
	{
		SyncGroupSizeAndOrder(MasterCollection, Pair.Key);
	}
}

void FManagedArrayCollection::CopyAttribute(const FManagedArrayCollection& MasterCollection, FName Name, FName Group)
{
	SyncGroupSizeAndOrder(MasterCollection, Group);
	FKeyType Key = FManagedArrayCollection::MakeMapKey(Name, Group);
	
	const FValueType& OriginalValue = MasterCollection.Map[Key];
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

			const void * PointerAddress = static_cast<const void*>(Value.Value);
			std::stringstream AddressStream;
			AddressStream << PointerAddress;

			Buffer += GroupName.ToString() + ":" + AttributeName.ToString() + " ["+ FString(AddressStream.str().c_str()) +"]\n";
		}
	}
	return Buffer;
}


void FManagedArrayCollection::Serialize(Chaos::FChaosArchive& Ar)
{
	int Version = 4;
	Ar << Version;

	if(Ar.IsLoading())
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