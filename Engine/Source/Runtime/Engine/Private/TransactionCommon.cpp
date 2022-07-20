// Copyright Epic Games, Inc. All Rights Reserved.

#include "TransactionCommon.h"
#include "Misc/ITransaction.h"
#include "UObject/UnrealType.h"
#include "Components/ActorComponent.h"

FTransactionPersistentObjectRef::FTransactionPersistentObjectRef(UObject* InObject)
{
	RootObject = InObject;
	{
		auto UseOuter = [](const UObject* Obj)
		{
			if (Obj == nullptr)
			{
				return false;
			}

			const bool bIsCDO = Obj->HasAllFlags(RF_ClassDefaultObject);
			const UObject* CDO = bIsCDO ? Obj : nullptr;
			const bool bIsClassCDO = (CDO != nullptr) ? (CDO->GetClass()->ClassDefaultObject == CDO) : false;
			if (!bIsClassCDO && CDO)
			{
				// Likely a trashed CDO, try to recover
				// Only known cause of this is ambiguous use of DSOs
				CDO = CDO->GetClass()->ClassDefaultObject;
			}
			const UActorComponent* AsComponent = Cast<UActorComponent>(Obj);
			const bool bIsDSO = Obj->HasAnyFlags(RF_DefaultSubObject);
			const bool bIsSCSComponent = AsComponent && AsComponent->IsCreatedByConstructionScript();
			return (bIsCDO && bIsClassCDO) || bIsDSO || bIsSCSComponent;
		};

		while (UseOuter(RootObject))
		{
			SubObjectHierarchyIDs.Add(RootObject->GetFName());
			RootObject = RootObject->GetOuter();
		}
	}
	check(RootObject);

	if (SubObjectHierarchyIDs.Num() > 0)
	{
		ReferenceType = EReferenceType::SubObject;
		Algo::Reverse(SubObjectHierarchyIDs);
	}
	else
	{
		ReferenceType = EReferenceType::RootObject;
	}

	// Make sure that when we look up the object we find the same thing:
	checkSlow(Get() == InObject);
}

UObject* FTransactionPersistentObjectRef::Get() const
{
	if (ReferenceType == EReferenceType::SubObject)
	{
		check(SubObjectHierarchyIDs.Num() > 0);

		UObject* CurrentObject = nullptr;

		// Do we have a valid cached pointer?
		if (!CachedRootObject.IsExplicitlyNull() && SubObjectHierarchyIDs.Num() == CachedSubObjectHierarchy.Num())
		{
			// Root object is a pointer test
			{
				CurrentObject = CachedRootObject.GetEvenIfUnreachable();
				if (CurrentObject != RootObject)
				{
					CurrentObject = nullptr;
				}
			}

			// All other sub-objects are a name test
			for (int32 SubObjectIndex = 0; CurrentObject && SubObjectIndex < SubObjectHierarchyIDs.Num(); ++SubObjectIndex)
			{
				CurrentObject = CachedSubObjectHierarchy[SubObjectIndex].GetEvenIfUnreachable();
				if (CurrentObject && CurrentObject->GetFName() != SubObjectHierarchyIDs[SubObjectIndex])
				{
					CurrentObject = nullptr;
				}
			}
		}
		if (CurrentObject)
		{
			return CurrentObject;
		}

		// Cached pointer is invalid
		CachedRootObject.Reset();
		CachedSubObjectHierarchy.Reset();

		// Try to find and cache the subobject
		CachedRootObject = RootObject;
		CurrentObject = RootObject;
		for (int32 SubObjectIndex = 0; CurrentObject && SubObjectIndex < SubObjectHierarchyIDs.Num(); ++SubObjectIndex)
		{
			CurrentObject = StaticFindObjectFast(UObject::StaticClass(), CurrentObject, SubObjectHierarchyIDs[SubObjectIndex]);
			CachedSubObjectHierarchy.Add(CurrentObject);
		}
		if (CurrentObject)
		{
			check(!CachedRootObject.IsExplicitlyNull() && SubObjectHierarchyIDs.Num() == CachedSubObjectHierarchy.Num());
			return CurrentObject;
		}

		// Cached pointer is invalid
		CachedRootObject.Reset();
		CachedSubObjectHierarchy.Reset();

		return nullptr;
	}

	return RootObject;
}

void FTransactionPersistentObjectRef::AddStructReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(RootObject);

	if (ReferenceType == EReferenceType::SubObject)
	{
		// We can't refresh the resolved pointers during ARO, as it's not safe to call FindObject to update the cache if stale
		// Instead we'll just ARO whatever we may have cached, as this may result in the resolved pointers being updated anyway
		// Note: This is needed as sub-objects may be subject to GC while inside the transaction buffer, as the references from their root 
		// object may have been removed (eg, a component on an actor will no longer be referenced by the actor after a delete operation)
		for (TWeakObjectPtr<UObject>& CachedSubObject : CachedSubObjectHierarchy)
		{
			UObject* CachedSubObjectPtr = CachedSubObject.GetEvenIfUnreachable();
			Collector.AddReferencedObject(CachedSubObjectPtr);
			CachedSubObject = CachedSubObjectPtr;
		}
	}
}


FTransactionSerializedTaggedData FTransactionSerializedTaggedData::FromOffsetAndSize(const int64 InOffset, const int64 InSize)
{
	return FTransactionSerializedTaggedData{ InOffset, InSize };
}

FTransactionSerializedTaggedData FTransactionSerializedTaggedData::FromStartAndEnd(const int64 InStart, const int64 InEnd)
{
	return FTransactionSerializedTaggedData{ InStart, InEnd - InStart };
}

void FTransactionSerializedTaggedData::AppendSerializedData(const int64 InOffset, const int64 InSize)
{
	if (DataOffset == INDEX_NONE)
	{
		DataOffset = InOffset;
		DataSize = InSize;
	}
	else
	{
		DataOffset = FMath::Min(DataOffset, InOffset);
		DataSize = FMath::Max(InOffset + InSize - DataOffset, DataSize);
	}
}

void FTransactionSerializedTaggedData::AppendSerializedData(const FTransactionSerializedTaggedData& InData)
{
	if (InData.HasSerializedData())
	{
		AppendSerializedData(InData.DataOffset, InData.DataSize);
	}
}

bool FTransactionSerializedTaggedData::HasSerializedData() const
{
	return DataOffset != INDEX_NONE && DataSize != 0;
}


void FTransactionSerializedObjectData::Read(void* Dest, int64 Offset, int64 Num) const
{
	checkSlow(Offset + Num <= Data.Num());
	FMemory::Memcpy(Dest, &Data[Offset], Num);
}

void FTransactionSerializedObjectData::Write(const void* Src, int64 Offset, int64 Num)
{
	if (Offset == Data.Num())
	{
		Data.AddUninitialized(Num);
	}
	FMemory::Memcpy(&Data[Offset], Src, Num);
}


namespace UE::Transaction
{

const FName TaggedDataKey_UnknownData = ".UnknownData";
const FName TaggedDataKey_ScriptData = ".ScriptData";

FSerializedObjectDataReader::FSerializedObjectDataReader(const FTransactionSerializedObject& InSerializedObject)
	: SerializedObject(InSerializedObject)
{
	SetIsLoading(true);
}

void FSerializedObjectDataReader::Serialize(void* SerData, int64 Num)
{
	if (Num)
	{
		SerializedObject.SerializedData.Read(SerData, Offset, Num);
		Offset += Num;
	}
}

FArchive& FSerializedObjectDataReader::operator<<(FName& N)
{
	int32 NameIndex = 0;
	*this << NameIndex;
	N = SerializedObject.ReferencedNames[NameIndex];
	return *this;
}

FArchive& FSerializedObjectDataReader::operator<<(UObject*& Res)
{
	int32 ObjectIndex = 0;
	*this << ObjectIndex;
	if (ObjectIndex != INDEX_NONE)
	{
		Res = SerializedObject.ReferencedObjects[ObjectIndex].Get();
	}
	else
	{
		Res = nullptr;
	}
	return *this;
}


FSerializedObjectDataWriter::FSerializedObjectDataWriter(FTransactionSerializedObject& InSerializedObject)
	: SerializedObject(InSerializedObject)
{
	SetIsSaving(true);

	for (int32 ObjIndex = 0; ObjIndex < SerializedObject.ReferencedObjects.Num(); ++ObjIndex)
	{
		ObjectMap.Add(SerializedObject.ReferencedObjects[ObjIndex].Get(), ObjIndex);
	}

	for (int32 NameIndex = 0; NameIndex < SerializedObject.ReferencedNames.Num(); ++NameIndex)
	{
		NameMap.Add(SerializedObject.ReferencedNames[NameIndex], NameIndex);
	}
}

FName FSerializedObjectDataWriter::GetTaggedDataKey() const
{
	FName TaggedDataKey;

	// Is this known property data?
	if (TaggedDataKey.IsNone())
	{
		const FArchiveSerializedPropertyChain* PropertyChain = GetSerializedPropertyChain();
		if (PropertyChain && PropertyChain->GetNumProperties() > 0)
		{
			TaggedDataKey = CachedSerializedTaggedPropertyKey.SyncCache(PropertyChain);
		}
	}

	// Is this script data?
	if (TaggedDataKey.IsNone() && bIsPerformingScriptSerialization)
	{
		bWasUsingTaggedDataKey_ScriptData = true;
		TaggedDataKey = FName(TaggedDataKey_ScriptData, TaggedDataKeyIndex_ScriptData);
	}
	else if (bWasUsingTaggedDataKey_ScriptData)
	{
		++TaggedDataKeyIndex_ScriptData;
		bWasUsingTaggedDataKey_ScriptData = false;
	}

	// Is this unknown data?
	if (TaggedDataKey.IsNone())
	{
		bWasUsingTaggedDataKey_UnknownData = true;
		TaggedDataKey = FName(TaggedDataKey_UnknownData, TaggedDataKeyIndex_UnknownData);
	}
	else if (bWasUsingTaggedDataKey_UnknownData)
	{
		++TaggedDataKeyIndex_UnknownData;
		bWasUsingTaggedDataKey_UnknownData = false;
	}

	return TaggedDataKey;
}

bool FSerializedObjectDataWriter::DoesObjectMatchSerializedObject(const UObject* Obj) const
{
	FNameBuilder ObjPathName;
	Obj->GetPathName(nullptr, ObjPathName);
	return FName(ObjPathName) == SerializedObject.ObjectPathName;
}

void FSerializedObjectDataWriter::MarkScriptSerializationStart(const UObject* Obj)
{
	FArchiveUObject::MarkScriptSerializationStart(Obj);

	if (DoesObjectMatchSerializedObject(Obj))
	{
		bIsPerformingScriptSerialization = true;
	}
}

void FSerializedObjectDataWriter::MarkScriptSerializationEnd(const UObject* Obj)
{
	FArchiveUObject::MarkScriptSerializationEnd(Obj);

	if (DoesObjectMatchSerializedObject(Obj))
	{
		bIsPerformingScriptSerialization = false;
	}
}

void FSerializedObjectDataWriter::Serialize(void* SerData, int64 Num)
{
	if (Num)
	{
		int64 DataIndex = Offset;
		SerializedObject.SerializedData.Write(SerData, Offset, Num);
		Offset += Num;

		// Track this offset index in the serialized data
		const FName SerializedTaggedDataKey = GetTaggedDataKey();
		if (!SerializedTaggedDataKey.IsNone())
		{
			FTransactionSerializedTaggedData& SerializedTaggedData = SerializedObject.SerializedTaggedData.FindOrAdd(SerializedTaggedDataKey);
			SerializedTaggedData.AppendSerializedData(DataIndex, Num);
		}
	}
}

FArchive& FSerializedObjectDataWriter::operator<<(FName& N)
{
	int32 NameIndex = INDEX_NONE;
	const int32* NameIndexPtr = NameMap.Find(N);
	if (NameIndexPtr)
	{
		NameIndex = *NameIndexPtr;
	}
	else
	{
		NameIndex = SerializedObject.ReferencedNames.Add(N);
		NameMap.Add(N, NameIndex);
	}

	// Track this name index in the serialized data
	const FName SerializedTaggedDataKey = GetTaggedDataKey();
	if (!SerializedTaggedDataKey.IsNone())
	{
		SerializedObject.SerializedNameIndices.Add(SerializedTaggedDataKey).Indices.AddUnique(NameIndex);
	}

	return *this << NameIndex;
}

FArchive& FSerializedObjectDataWriter::operator<<(UObject*& Res)
{
	int32 ObjectIndex = INDEX_NONE;
	const int32* ObjIndexPtr = ObjectMap.Find(Res);
	if (ObjIndexPtr)
	{
		ObjectIndex = *ObjIndexPtr;
	}
	else if (Res)
	{
		ObjectIndex = SerializedObject.ReferencedObjects.Add(FTransactionPersistentObjectRef(Res));
		ObjectMap.Add(Res, ObjectIndex);
	}

	// Track this object offset in the serialized data
	const FName SerializedTaggedDataKey = GetTaggedDataKey();
	if (!SerializedTaggedDataKey.IsNone())
	{
		SerializedObject.SerializedObjectIndices.Add(SerializedTaggedDataKey).Indices.AddUnique(ObjectIndex);
	}

	return *this << ObjectIndex;
}

FName FSerializedObjectDataWriter::FCachedPropertyKey::SyncCache(const FArchiveSerializedPropertyChain* InPropertyChain)
{
	if (InPropertyChain)
	{
		const uint32 CurrentUpdateCount = InPropertyChain->GetUpdateCount();
		if (CurrentUpdateCount != LastUpdateCount)
		{
			CachedKey = InPropertyChain->GetNumProperties() > 0 ? InPropertyChain->GetPropertyFromRoot(0)->GetFName() : FName();
			LastUpdateCount = CurrentUpdateCount;
		}
	}
	else
	{
		CachedKey = FName();
		LastUpdateCount = 0;
	}

	return CachedKey;
}


namespace DiffUtil
{

void GenerateObjectDiff(const FTransactionSerializedObject& OldSerializedObject, const FTransactionSerializedObject& NewSerializedObject, FTransactionObjectDeltaChange& OutDeltaChange, const bool bFullDiff)
{
	auto IsTaggedDataBlockIdentical = [&OldSerializedObject, &NewSerializedObject, &OutDeltaChange](const FName TaggedDataKey, const FTransactionSerializedTaggedData& OldSerializedTaggedData, const FTransactionSerializedTaggedData& NewSerializedTaggedData) -> bool
	{
		// Binary compare the serialized data to see if something has changed for this property
		bool bIsTaggedDataIdentical = OldSerializedTaggedData.DataSize == NewSerializedTaggedData.DataSize;
		if (bIsTaggedDataIdentical && OldSerializedTaggedData.HasSerializedData())
		{
			bIsTaggedDataIdentical = FMemory::Memcmp(OldSerializedObject.SerializedData.GetPtr(OldSerializedTaggedData.DataOffset), NewSerializedObject.SerializedData.GetPtr(NewSerializedTaggedData.DataOffset), NewSerializedTaggedData.DataSize) == 0;
		}
		if (bIsTaggedDataIdentical)
		{
			bIsTaggedDataIdentical = Internal::AreObjectPointersIdentical(OldSerializedObject, NewSerializedObject, TaggedDataKey);
		}
		if (bIsTaggedDataIdentical)
		{
			bIsTaggedDataIdentical = Internal::AreNamesIdentical(OldSerializedObject, NewSerializedObject, TaggedDataKey);
		}
		return bIsTaggedDataIdentical;
	};

	auto ShouldCompareTaggedData = [](const FName TaggedDataKey) -> bool
	{
		if (TaggedDataKey.GetComparisonIndex() == TaggedDataKey_ScriptData.GetComparisonIndex())
		{
			// Never compare script data, as it's assumed to be overhead around the tagged property serialization
			return false;
		}

		return true;
	};

	auto ShouldCompareAsNonPropertyData = [](const FName TaggedDataKey)
	{
		return TaggedDataKey.GetComparisonIndex() == TaggedDataKey_UnknownData.GetComparisonIndex();
	};

	if (bFullDiff)
	{
		OutDeltaChange.bHasNameChange |= OldSerializedObject.ObjectName != NewSerializedObject.ObjectName;
		OutDeltaChange.bHasOuterChange |= OldSerializedObject.ObjectOuterPathName != NewSerializedObject.ObjectOuterPathName;
		OutDeltaChange.bHasExternalPackageChange |= OldSerializedObject.ObjectExternalPackageName != NewSerializedObject.ObjectExternalPackageName;
		OutDeltaChange.bHasPendingKillChange |= OldSerializedObject.bIsPendingKill != NewSerializedObject.bIsPendingKill;
	}

	for (const TPair<FName, FTransactionSerializedTaggedData>& NewNamePropertyPair : NewSerializedObject.SerializedTaggedData)
	{
		if (!ShouldCompareTaggedData(NewNamePropertyPair.Key))
		{
			continue;
		}

		const FTransactionSerializedTaggedData* OldSerializedTaggedData = OldSerializedObject.SerializedTaggedData.Find(NewNamePropertyPair.Key);
		if (ShouldCompareAsNonPropertyData(NewNamePropertyPair.Key))
		{
			if (bFullDiff && !OutDeltaChange.bHasNonPropertyChanges && (!OldSerializedTaggedData || !IsTaggedDataBlockIdentical(NewNamePropertyPair.Key, *OldSerializedTaggedData, NewNamePropertyPair.Value)))
			{
				OutDeltaChange.bHasNonPropertyChanges = true;
			}
		}
		else if (OldSerializedTaggedData)
		{
			if (!IsTaggedDataBlockIdentical(NewNamePropertyPair.Key, *OldSerializedTaggedData, NewNamePropertyPair.Value))
			{
				OutDeltaChange.ChangedProperties.AddUnique(NewNamePropertyPair.Key);
			}
		}
		else if (bFullDiff)
		{
			// Missing property, assume that the property changed
			OutDeltaChange.ChangedProperties.AddUnique(NewNamePropertyPair.Key);
		}
	}

	if (bFullDiff)
	{
		for (const TPair<FName, FTransactionSerializedTaggedData>& OldNamePropertyPair : OldSerializedObject.SerializedTaggedData)
		{
			if (!ShouldCompareTaggedData(OldNamePropertyPair.Key) || NewSerializedObject.SerializedTaggedData.Contains(OldNamePropertyPair.Key))
			{
				continue;
			}

			if (ShouldCompareAsNonPropertyData(OldNamePropertyPair.Key))
			{
				OutDeltaChange.bHasNonPropertyChanges = true;
			}
			else
			{
				// Missing property, assume that the property changed
				OutDeltaChange.ChangedProperties.AddUnique(OldNamePropertyPair.Key);
			}
		}
	}
}

namespace Internal
{

bool AreObjectPointersIdentical(const FTransactionSerializedObject& OldSerializedObject, const FTransactionSerializedObject& NewSerializedObject, const FName TaggedDataKey)
{
	auto GetIndicesArray = [&TaggedDataKey](const FTransactionSerializedObject& SerializedObject) -> const TArray<int32>&
	{
		if (const FTransactionSerializedIndices* SerializedObjectIndices = SerializedObject.SerializedObjectIndices.Find(TaggedDataKey))
		{
			return SerializedObjectIndices->Indices;
		}

		static const TArray<int32> EmptyIndicesArray;
		return EmptyIndicesArray;
	};

	const TArray<int32>& OldSerializedObjectIndices = GetIndicesArray(OldSerializedObject);
	const TArray<int32>& NewSerializedObjectIndices = GetIndicesArray(NewSerializedObject);

	bool bAreObjectPointersIdentical = OldSerializedObjectIndices.Num() == NewSerializedObjectIndices.Num();
	for (int32 ObjIndex = 0; ObjIndex < OldSerializedObjectIndices.Num() && bAreObjectPointersIdentical; ++ObjIndex)
	{
		const FTransactionPersistentObjectRef& OldObjectRef = OldSerializedObject.ReferencedObjects.IsValidIndex(OldSerializedObjectIndices[ObjIndex]) ? OldSerializedObject.ReferencedObjects[OldSerializedObjectIndices[ObjIndex]] : FTransactionPersistentObjectRef();
		const FTransactionPersistentObjectRef& NewObjectRef = NewSerializedObject.ReferencedObjects.IsValidIndex(NewSerializedObjectIndices[ObjIndex]) ? NewSerializedObject.ReferencedObjects[NewSerializedObjectIndices[ObjIndex]] : FTransactionPersistentObjectRef();
		bAreObjectPointersIdentical = OldObjectRef == NewObjectRef;
	}
	return bAreObjectPointersIdentical;
}

bool AreNamesIdentical(const FTransactionSerializedObject& OldSerializedObject, const FTransactionSerializedObject& NewSerializedObject, const FName TaggedDataKey)
{
	auto GetIndicesArray = [&TaggedDataKey](const FTransactionSerializedObject& SerializedObject) -> const TArray<int32>&
	{
		if (const FTransactionSerializedIndices* SerializedNameIndices = SerializedObject.SerializedNameIndices.Find(TaggedDataKey))
		{
			return SerializedNameIndices->Indices;
		}

		static const TArray<int32> EmptyIndicesArray;
		return EmptyIndicesArray;
	};

	const TArray<int32>& OldSerializedNameIndices = GetIndicesArray(OldSerializedObject);
	const TArray<int32>& NewSerializedNameIndices = GetIndicesArray(NewSerializedObject);

	bool bAreNamesIdentical = OldSerializedNameIndices.Num() == NewSerializedNameIndices.Num();
	for (int32 ObjIndex = 0; ObjIndex < OldSerializedNameIndices.Num() && bAreNamesIdentical; ++ObjIndex)
	{
		const FName& OldName = OldSerializedObject.ReferencedNames.IsValidIndex(OldSerializedNameIndices[ObjIndex]) ? OldSerializedObject.ReferencedNames[OldSerializedNameIndices[ObjIndex]] : FName();
		const FName& NewName = NewSerializedObject.ReferencedNames.IsValidIndex(NewSerializedNameIndices[ObjIndex]) ? NewSerializedObject.ReferencedNames[NewSerializedNameIndices[ObjIndex]] : FName();
		bAreNamesIdentical = OldName == NewName;
	}
	return bAreNamesIdentical;
}

} // namespace Internal

} // namespace DiffUtil

} // namespace UE::Transaction
