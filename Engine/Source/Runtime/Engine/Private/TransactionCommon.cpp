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


FName FTransactionSerializedProperty::BuildSerializedPropertyKey(const FArchiveSerializedPropertyChain& InPropertyChain)
{
	const int32 NumProperties = InPropertyChain.GetNumProperties();
	check(NumProperties > 0);
	return InPropertyChain.GetPropertyFromRoot(0)->GetFName();
}

void FTransactionSerializedProperty::AppendSerializedData(const int64 InOffset, const int64 InSize)
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

FSerializedObjectDataReader::FSerializedObjectDataReader(const FTransactionSerializedObject& InSerializedObject)
	: SerializedObject(InSerializedObject)
	, Offset(0)
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
	, Offset(0)
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

void FSerializedObjectDataWriter::Serialize(void* SerData, int64 Num)
{
	if (Num)
	{
		int64 DataIndex = Offset;
		SerializedObject.SerializedData.Write(SerData, Offset, Num);
		Offset += Num;

		// Track this property offset in the serialized data
		const FArchiveSerializedPropertyChain* PropertyChain = GetSerializedPropertyChain();
		if (PropertyChain && PropertyChain->GetNumProperties() > 0)
		{
			const FName SerializedTaggedPropertyKey = CachedSerializedTaggedPropertyKey.SyncCache(PropertyChain);
			FTransactionSerializedProperty& SerializedTaggedProperty = SerializedObject.SerializedProperties.FindOrAdd(SerializedTaggedPropertyKey);
			SerializedTaggedProperty.AppendSerializedData(DataIndex, Num);
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
	{
		const FArchiveSerializedPropertyChain* PropertyChain = GetSerializedPropertyChain();
		const FName SerializedTaggedPropertyKey = CachedSerializedTaggedPropertyKey.SyncCache(PropertyChain);
		SerializedObject.SerializedNameIndices.Add(SerializedTaggedPropertyKey).Indices.AddUnique(NameIndex);
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
	{
		const FArchiveSerializedPropertyChain* PropertyChain = GetSerializedPropertyChain();
		const FName SerializedTaggedPropertyKey = CachedSerializedTaggedPropertyKey.SyncCache(PropertyChain);
		SerializedObject.SerializedObjectIndices.Add(SerializedTaggedPropertyKey).Indices.AddUnique(ObjectIndex);
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
			CachedKey = InPropertyChain->GetNumProperties() > 0 ? FTransactionSerializedProperty::BuildSerializedPropertyKey(*InPropertyChain) : FName();
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
	if (bFullDiff)
	{
		OutDeltaChange.bHasNameChange |= OldSerializedObject.ObjectName != NewSerializedObject.ObjectName;
		OutDeltaChange.bHasOuterChange |= OldSerializedObject.ObjectOuterPathName != NewSerializedObject.ObjectOuterPathName;
		OutDeltaChange.bHasExternalPackageChange |= OldSerializedObject.ObjectExternalPackageName != NewSerializedObject.ObjectExternalPackageName;
		OutDeltaChange.bHasPendingKillChange |= OldSerializedObject.bIsPendingKill != NewSerializedObject.bIsPendingKill;

		if (!Internal::AreObjectPointersIdentical(OldSerializedObject, NewSerializedObject, NAME_None))
		{
			OutDeltaChange.bHasNonPropertyChanges = true;
		}

		if (!Internal::AreNamesIdentical(OldSerializedObject, NewSerializedObject, NAME_None))
		{
			OutDeltaChange.bHasNonPropertyChanges = true;
		}
	}

	if (OldSerializedObject.SerializedProperties.Num() > 0 || NewSerializedObject.SerializedProperties.Num() > 0)
	{
		int64 StartOfOldPropertyBlock = INT64_MAX;
		int64 StartOfNewPropertyBlock = INT64_MAX;
		int64 EndOfOldPropertyBlock = -1;
		int64 EndOfNewPropertyBlock = -1;

		for (const TPair<FName, FTransactionSerializedProperty>& NewNamePropertyPair : NewSerializedObject.SerializedProperties)
		{
			const FTransactionSerializedProperty* OldSerializedProperty = OldSerializedObject.SerializedProperties.Find(NewNamePropertyPair.Key);
			if (!OldSerializedProperty)
			{
				if (bFullDiff)
				{
					// Missing property, assume that the property changed
					OutDeltaChange.ChangedProperties.AddUnique(NewNamePropertyPair.Key);
				}
				continue;
			}

			// Update the tracking for the start/end of the property block within the serialized data
			StartOfOldPropertyBlock = FMath::Min(StartOfOldPropertyBlock, OldSerializedProperty->DataOffset);
			StartOfNewPropertyBlock = FMath::Min(StartOfNewPropertyBlock, NewNamePropertyPair.Value.DataOffset);
			EndOfOldPropertyBlock = FMath::Max(EndOfOldPropertyBlock, OldSerializedProperty->DataOffset + OldSerializedProperty->DataSize);
			EndOfNewPropertyBlock = FMath::Max(EndOfNewPropertyBlock, NewNamePropertyPair.Value.DataOffset + NewNamePropertyPair.Value.DataSize);

			// Binary compare the serialized data to see if something has changed for this property
			bool bIsPropertyIdentical = OldSerializedProperty->DataSize == NewNamePropertyPair.Value.DataSize;
			if (bIsPropertyIdentical && NewNamePropertyPair.Value.DataSize > 0)
			{
				bIsPropertyIdentical = FMemory::Memcmp(OldSerializedObject.SerializedData.GetPtr(OldSerializedProperty->DataOffset), NewSerializedObject.SerializedData.GetPtr(NewNamePropertyPair.Value.DataOffset), NewNamePropertyPair.Value.DataSize) == 0;
			}
			if (bIsPropertyIdentical)
			{
				bIsPropertyIdentical = Internal::AreObjectPointersIdentical(OldSerializedObject, NewSerializedObject, NewNamePropertyPair.Key);
			}
			if (bIsPropertyIdentical)
			{
				bIsPropertyIdentical = Internal::AreNamesIdentical(OldSerializedObject, NewSerializedObject, NewNamePropertyPair.Key);
			}

			if (!bIsPropertyIdentical)
			{
				OutDeltaChange.ChangedProperties.AddUnique(NewNamePropertyPair.Key);
			}
		}

		for (const TPair<FName, FTransactionSerializedProperty>& OldNamePropertyPair : OldSerializedObject.SerializedProperties)
		{
			const FTransactionSerializedProperty* NewSerializedProperty = NewSerializedObject.SerializedProperties.Find(OldNamePropertyPair.Key);
			if (!NewSerializedProperty)
			{
				if (bFullDiff)
				{
					// Missing property, assume that the property changed
					OutDeltaChange.ChangedProperties.AddUnique(OldNamePropertyPair.Key);
				}
				continue;
			}
		}

		if (bFullDiff)
		{
			// Compare the data before the property block to see if something else in the object has changed
			if (!OutDeltaChange.bHasNonPropertyChanges)
			{
				const int64 OldHeaderSize = FMath::Min(StartOfOldPropertyBlock, OldSerializedObject.SerializedData.Num());
				const int64 CurrentHeaderSize = FMath::Min(StartOfNewPropertyBlock, NewSerializedObject.SerializedData.Num());

				bool bIsHeaderIdentical = OldHeaderSize == CurrentHeaderSize;
				if (bIsHeaderIdentical && CurrentHeaderSize > 0)
				{
					bIsHeaderIdentical = FMemory::Memcmp(OldSerializedObject.SerializedData.GetPtr(0), NewSerializedObject.SerializedData.GetPtr(0), CurrentHeaderSize) == 0;
				}

				if (!bIsHeaderIdentical)
				{
					OutDeltaChange.bHasNonPropertyChanges = true;
				}
			}

			// Compare the data after the property block to see if something else in the object has changed
			if (!OutDeltaChange.bHasNonPropertyChanges)
			{
				const int64 OldFooterSize = OldSerializedObject.SerializedData.Num() - FMath::Max<int64>(EndOfOldPropertyBlock, 0);
				const int64 CurrentFooterSize = NewSerializedObject.SerializedData.Num() - FMath::Max<int64>(EndOfNewPropertyBlock, 0);

				bool bIsFooterIdentical = OldFooterSize == CurrentFooterSize;
				if (bIsFooterIdentical && CurrentFooterSize > 0)
				{
					bIsFooterIdentical = FMemory::Memcmp(OldSerializedObject.SerializedData.GetPtr(EndOfOldPropertyBlock), NewSerializedObject.SerializedData.GetPtr(EndOfNewPropertyBlock), CurrentFooterSize) == 0;
				}

				if (!bIsFooterIdentical)
				{
					OutDeltaChange.bHasNonPropertyChanges = true;
				}
			}
		}
	}
	else if (bFullDiff)
	{
		// No properties, so just compare the whole blob
		bool bIsBlobIdentical = OldSerializedObject.SerializedData.Num() == NewSerializedObject.SerializedData.Num();
		if (bIsBlobIdentical && NewSerializedObject.SerializedData.Num() > 0)
		{
			bIsBlobIdentical = FMemory::Memcmp(OldSerializedObject.SerializedData.GetPtr(0), NewSerializedObject.SerializedData.GetPtr(0), NewSerializedObject.SerializedData.Num()) == 0;
		}

		if (!bIsBlobIdentical)
		{
			OutDeltaChange.bHasNonPropertyChanges = true;
		}
	}
}

namespace Internal
{

bool AreObjectPointersIdentical(const FTransactionSerializedObject& OldSerializedObject, const FTransactionSerializedObject& NewSerializedObject, const FName PropertyName)
{
	auto GetIndicesArray = [&PropertyName](const FTransactionSerializedObject& SerializedObject) -> const TArray<int32>&
	{
		if (const FTransactionSerializedIndices* SerializedObjectIndices = SerializedObject.SerializedObjectIndices.Find(PropertyName))
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

bool AreNamesIdentical(const FTransactionSerializedObject& OldSerializedObject, const FTransactionSerializedObject& NewSerializedObject, const FName PropertyName)
{
	auto GetIndicesArray = [&PropertyName](const FTransactionSerializedObject& SerializedObject) -> const TArray<int32>&
	{
		if (const FTransactionSerializedIndices* SerializedNameIndices = SerializedObject.SerializedNameIndices.Find(PropertyName))
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
