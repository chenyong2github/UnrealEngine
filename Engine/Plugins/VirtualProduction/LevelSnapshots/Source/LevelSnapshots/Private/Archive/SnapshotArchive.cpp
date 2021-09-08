// Copyright Epic Games, Inc. All Rights Reserved.

#include "SnapshotArchive.h"

#include "ObjectSnapshotData.h"
#include "SnapshotRestorability.h"
#include "SnapshotVersion.h"
#include "WorldSnapshotData.h"

#include "Internationalization/TextNamespaceUtil.h"
#include "Internationalization/TextPackageNamespaceUtil.h"
#include "UObject/ObjectMacros.h"
#include "Util/SnapshotObjectUtil.h"

void FSnapshotArchive::ApplyToSnapshotWorldObject(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, UObject* InObjectToRestore, UPackage* InLocalisationSnapshotPackage)
{
	ApplyToSnapshotWorldObject(
		InObjectData,
		InSharedData,
		InObjectToRestore,
#if USE_STABLE_LOCALIZATION_KEYS
		TextNamespaceUtil::EnsurePackageNamespace(InLocalisationSnapshotPackage)
#else
		FString()
#endif
		);
}

void FSnapshotArchive::ApplyToSnapshotWorldObject(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, UObject* InObjectToRestore, const FString& InLocalisationNamespace)
{
	FSnapshotArchive Archive(InObjectData, InSharedData, true, InObjectToRestore);
#if USE_STABLE_LOCALIZATION_KEYS
	Archive.SetLocalizationNamespace(InLocalisationNamespace);
#endif

	InObjectToRestore->Serialize(Archive);
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
	const bool bIsPropertyUnsupported = InProperty->HasAnyPropertyFlags(ExcludedPropertyFlags);
	return bIsPropertyUnsupported || !FSnapshotRestorability::IsPropertyDesirableForCapture(InProperty);
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

		Value = ResolveObjectDependency(ReferencedIndex);
	}
	else
	{
		int32 ReferenceIndex = SnapshotUtil::Object::AddObjectDependency(SharedData, Value);
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

UObject* FSnapshotArchive::ResolveObjectDependency(int32 ObjectIndex) const
{
	return SnapshotUtil::Object::ResolveObjectDependencyForSnapshotWorld(SharedData, ObjectIndex, GetLocalizationNamespace());
}

FSnapshotArchive::FSnapshotArchive(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, bool bIsLoading, UObject* InSerializedObject)
	:
	ExcludedPropertyFlags(CPF_BlueprintAssignable | CPF_Transient | CPF_Deprecated),
	SerializedObject(InSerializedObject),
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
		Super::SetIsSaving(false);
	}
	else
	{
		Super::SetIsLoading(false);
		Super::SetIsSaving(true);
	}

	if (bIsLoading)
	{
		InSharedData.GetSnapshotVersionInfo().ApplyToArchive(*this);
	}
}
