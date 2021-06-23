// Copyright Epic Games, Inc. All Rights Reserved.

#include "SnapshotArchive.h"

#include "ObjectSnapshotData.h"
#include "SnapshotRestorability.h"
#include "SnapshotVersion.h"
#include "WorldSnapshotData.h"

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "Internationalization/TextNamespaceUtil.h"
#include "Internationalization/TextPackageNamespaceUtil.h"
#include "Serialization/ArchiveSerializedPropertyChain.h"
#include "UObject/ObjectMacros.h"

void FSnapshotArchive::ApplyToSnapshotWorldObject(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, UObject* InObjectToRestore, UPackage* InLocalisationSnapshotPackage)
{
	FSnapshotArchive Archive(InObjectData, InSharedData, true, InObjectToRestore);
#if USE_STABLE_LOCALIZATION_KEYS
	Archive.SetLocalizationNamespace(TextNamespaceUtil::EnsurePackageNamespace(InLocalisationSnapshotPackage));
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
	const bool bIsPropertyUnsupported = InProperty->HasAnyPropertyFlags(ExcludedPropertyFlags)|| IsPropertyReferenceToSubobject(InProperty);
	const bool bIsBlacklisted = FSnapshotRestorability::IsPropertyBlacklistedForCapture(InProperty);
	const bool bIsWhitelisted = FSnapshotRestorability::IsPropertyWhitelistedForCapture(InProperty);
	return bIsPropertyUnsupported || bIsBlacklisted || (!bIsPropertyUnsupported && bIsWhitelisted);
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
		int32 ReferenceIndex = SharedData.AddObjectDependency(Value);
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
	return SharedData.ResolveObjectDependencyForSnapshotWorld(ObjectIndex);
}

bool FSnapshotArchive::IsPropertyReferenceToSubobject(const FProperty* InProperty) const
{
	const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(InProperty);
	if (!ObjectProperty)
	{
		return false;
	}

	const bool bIsMarkedAsSubobject = InProperty->HasAnyPropertyFlags(CPF_InstancedReference | CPF_ContainsInstancedReference | CPF_PersistentInstance);
	const bool bIsComponentPtr = ObjectProperty->PropertyClass->IsChildOf(UActorComponent::StaticClass());
	if (bIsMarkedAsSubobject || bIsComponentPtr)
	{
		return true;
	}

	// TODO: Walk GetSerializedPropertyChain to get the value ptr of ObjectProperty. Then check whether contained object IsIn(GetSerializedObject()). Extra complicated because sometimes values may be in arrays, set, and tmaps.
	return false;
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
