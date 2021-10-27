// Copyright Epic Games, Inc. All Rights Reserved.

#include "SnapshotArchive.h"

#include "LevelSnapshotsModule.h"
#include "ObjectSnapshotData.h"
#include "SnapshotRestorability.h"
#include "SnapshotVersion.h"
#include "WorldSnapshotData.h"
#include "Util/SnapshotObjectUtil.h"
#if UE_BUILD_DEBUG
#include "SnapshotConsoleVariables.h"
#endif

#include "UObject/ObjectMacros.h"

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
#if UE_BUILD_DEBUG
	FString PropertyToDebug = SnapshotCVars::CVarBreakOnSerializedPropertyName.GetValueOnAnyThread();
	if (!PropertyToDebug.IsEmpty() && InProperty->GetName().Equals(PropertyToDebug, ESearchCase::IgnoreCase))
	{
		UE_DEBUG_BREAK();
	}
#endif
	
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

FSnapshotArchive::FSnapshotArchive(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, bool bIsLoading, UObject* InSerializedObject)
	:
	ExcludedPropertyFlags(CPF_BlueprintAssignable | CPF_Transient | CPF_Deprecated),
	SerializedObject(InSerializedObject),
    ObjectData(InObjectData),
    SharedData(InSharedData)
{
	Super::SetWantBinaryPropertySerialization(false);
	Super::SetIsTransacting(false);
	Super::SetIsPersistent(true);
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
