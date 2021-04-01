// Copyright Epic Games, Inc. All Rights Reserved.

#include "SnapshotArchive.h"

#include "ObjectSnapshotData.h"
#include "WorldSnapshotData.h"

#include "UObject/ObjectMacros.h"

FSnapshotArchive FSnapshotArchive::MakeArchiveForRestoring(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData)
{
	return FSnapshotArchive(InObjectData, InSharedData, true);
}

FString FSnapshotArchive::GetArchiveName() const
{
	return TEXT("FSnapshotArchive");
}

int64 FSnapshotArchive::TotalSize()
{
	return ObjectData.SerializedData.Num();
}

int64 FSnapshotArchive::Tell()
{
	return DataIndex;
}

void FSnapshotArchive::Seek(int64 InPos)
{
	checkSlow(InPos <= TotalSize());
	DataIndex = InPos;
}

bool FSnapshotArchive::ShouldSkipProperty(const FProperty* InProperty) const
{
	return InProperty->HasAnyPropertyFlags(ExcludedPropertyFlags)
		// We do not support (instanced) subobjects at this moment
		|| InProperty->HasAnyPropertyFlags(CPF_InstancedReference | CPF_ContainsInstancedReference | CPF_PersistentInstance);
}

FArchive& FSnapshotArchive::operator<<(FName& Value)
{
	if (IsLoading())
	{
		int32 NameIndex;
		*this << NameIndex;

		if (!ensureAlwaysMsgf(SharedData.SerializedNames.IsValidIndex(NameIndex), TEXT("Data appears to be corrupted")))
		{
			SetError();
			return *this;
		}
		
		Value = SharedData.SerializedNames[NameIndex];
	}
	else
	{
		int32 NameIndex = SharedData.SerializedNames.Add(Value);
		*this << NameIndex;
	}
	
	return *this;
}

FArchive& FSnapshotArchive::operator<<(UObject*& Value)
{
	if (IsLoading())
	{
		int32 ReferencedIndex;
		*this << ReferencedIndex;

		if (!ensureAlwaysMsgf(SharedData.SerializedObjectReferences.IsValidIndex(ReferencedIndex), TEXT("Data appears to be corrupted")))
		{
			SetError();
			return *this;
		}
		
		const FSoftObjectPath& ObjectPath = SharedData.SerializedObjectReferences[ReferencedIndex];
		if (ObjectPath.IsNull())
		{
			Value = nullptr;
			return *this;
		}

		TOptional<UObject*> Resolved = SharedData.ResolveObjectDependency(ReferencedIndex, bShouldLoadObjectDependenciesForTempWorld ? FWorldSnapshotData::EResolveType::ResolveForUseInTempWorld : FWorldSnapshotData::EResolveType::ResolveForUseInOriginalWorld);
		Value = Resolved.Get(nullptr);
	}
	else
	{
		int32 ReferenceIndex = SharedData.AddObjectDependency(Value);
		// TODO: Check whether subobject and allocate
		*this << ReferenceIndex;
	}
	
	return *this;
}

void FSnapshotArchive::Serialize(void* Data, int64 Length)
{
	if (Length <= 0)
	{
		return;
	}

	if (IsLoading())
	{
		if (!ensure(DataIndex + Length <= TotalSize()))
		{
			SetError();
			return;
		}
		
		FMemory::Memcpy(Data, &ObjectData.SerializedData[DataIndex], Length);
		DataIndex += Length;
	}
	else
	{
		const int64 RequiredEndIndex = DataIndex + Length;
		const int32 ToAlloc = RequiredEndIndex - TotalSize();
		if (ToAlloc > 0)
		{
			ObjectData.SerializedData.AddUninitialized(ToAlloc);
		}
		FMemory::Memcpy(&ObjectData.SerializedData[DataIndex], Data, Length);
		DataIndex = RequiredEndIndex;
	}
}

FSnapshotArchive::FSnapshotArchive(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, bool bIsLoading)
	:
	ExcludedPropertyFlags(CPF_BlueprintAssignable | CPF_Transient | CPF_Deprecated),
    ObjectData(InObjectData),
    SharedData(InSharedData)
{
	Super::SetWantBinaryPropertySerialization(false);
	Super::SetIsTransacting(false);
	Super::SetIsPersistent(!bIsLoading);
	ArNoDelta = true;

	if (bIsLoading)
	{
		Super::SetIsLoading(true);
	}
	else
	{
		Super::SetIsSaving(true);
	}
}
