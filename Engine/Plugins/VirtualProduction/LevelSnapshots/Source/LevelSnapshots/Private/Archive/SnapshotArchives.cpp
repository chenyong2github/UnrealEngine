// Copyright Epic Games, Inc. All Rights Reserved.

#include "SnapshotArchives.h"

#include "BaseObjectInfo.h"
#include "LevelSnapshotSelections.h"
#include "LevelSnapshotsStats.h"

#include "HAL/UnrealMemory.h"
#include "Serialization/ArchiveSerializedPropertyChain.h"

// Set up the property flags required by the archive
uint64 FObjectSnapshotArchive::RequiredPropertyFlags = CPF_Edit | CPF_EditConst;
uint64 FObjectSnapshotArchive::ExcludedPropertyFlags = CPF_BlueprintAssignable;

namespace
{
	bool IsInstancedProperty(const FProperty* InProperty)
	{
		return InProperty->HasAnyPropertyFlags(CPF_InstancedReference | CPF_ContainsInstancedReference | CPF_PersistentInstance);
	}
}

void FObjectSnapshotArchive::SetWritableObjectInfo(FBaseObjectInfo& InObjectInfo)
{
	ObjectInfoUnion.ObjectInfo = &InObjectInfo;
}

void FObjectSnapshotArchive::SetReadOnlyObjectInfo(const FBaseObjectInfo& InObjectInfo)
{
	ObjectInfoUnion.ConstObjectInfo = &InObjectInfo;
}

FString FObjectSnapshotArchive::GetArchiveName() const 
{
	return TEXT("FObjectSnapshotArchive");
}

int64 FObjectSnapshotArchive::TotalSize()
{
	return ObjectInfoUnion.ConstObjectInfo->SerializedData.Data.Num();
}

int64 FObjectSnapshotArchive::Tell() 
{
	return Offset;
}

void FObjectSnapshotArchive::Seek(int64 InPos)
{
	checkSlow(Offset <= ObjectInfoUnion.ConstObjectInfo->SerializedData.Data.Num());
	Offset = InPos;
}

bool FObjectSnapshotArchive::ShouldSkipProperty(const FProperty* InProperty) const
{
	return IsInstancedProperty(InProperty) || InProperty->HasAnyPropertyFlags(ExcludedPropertyFlags);
}

void FObjectSnapshotArchive::Serialize(void* Data, int64 Num)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("Serialize"), STAT_Serialize, STATGROUP_LevelSnapshots);
	
	if (Num)
	{
		if (IsLoading())
		{
			const FBaseObjectInfo& ObjectInfo = *ObjectInfoUnion.ConstObjectInfo;
			if (Offset + Num <= ObjectInfo.SerializedData.Data.Num())
			{
				FMemory::Memcpy(Data, &ObjectInfo.SerializedData.Data[(int32)Offset], Num);
				Offset += Num;
			}
			else
			{
				SetError();
			}
		}
		else
		{
			FBaseObjectInfo& ObjectInfo = *ObjectInfoUnion.ObjectInfo;

			int32 ToAlloc = Offset + (int32)Num - ObjectInfo.SerializedData.Data.Num();
			if (ToAlloc > 0)
			{
				ObjectInfo.SerializedData.Data.AddUninitialized(ToAlloc);

				FLevelSnapshot_Property& Property = GetProperty();

				// Track this property offset in the serialized data, if actually allocated for it, and this isn't a seek back in the stream
				Property.AppendSerializedData((uint32)Offset, (uint32)ToAlloc);

				// if we are in a property block
				if (!Property.PropertyPath.Get())
				{
					// Map the start and end of the property block
					ObjectInfo.PropertyBlockStart = FMath::Min(ObjectInfo.PropertyBlockStart, (uint32)Offset);
					ObjectInfo.PropertyBlockEnd = FMath::Max(ObjectInfo.PropertyBlockEnd, (uint32)(Offset + ToAlloc));
				}
			}
			FMemory::Memcpy(&ObjectInfo.SerializedData.Data[(int32)Offset], Data, Num);
			Offset += Num;
		}
	}
}

FArchive& FObjectSnapshotArchive::operator<<(class FName& Name)
{
	int32 NameIndex;

	if (IsLoading())
	{
		const FBaseObjectInfo& ObjectInfo = *ObjectInfoUnion.ConstObjectInfo;
		*this << NameIndex;

		if (ObjectInfo.ReferencedNames.IsValidIndex(NameIndex))
		{
			Name = ObjectInfo.ReferencedNames[NameIndex];
		}
		else
		{
			SetError();
		}
	}
	else
	{
		FBaseObjectInfo& ObjectInfo = *ObjectInfoUnion.ObjectInfo;
		NameIndex = ObjectInfo.ReferencedNames.AddUnique(Name);

		// Track this name index as part of this property
		GetProperty().AddNameReference(Offset, NameIndex);
		*this << NameIndex;
	}

	return *this;
}

FArchive& FObjectSnapshotArchive::operator<<(class UObject*& Object)
{
	if (IsLoading())
	{
		const FBaseObjectInfo& ObjectInfo = *ObjectInfoUnion.ConstObjectInfo;
		int32 ObjectIndex;
		*this << ObjectIndex;
		
		if (IsInstancedProperty(GetSerializedProperty()))
		{
			return *this;
		}

		const bool bOriginalValueWasNull = ObjectIndex == INDEX_NONE;
		const bool bOriginalValueWasValidReference = ObjectInfo.ReferencedObjects.IsValidIndex(ObjectIndex);
		if (bOriginalValueWasValidReference)
		{
			// ObjectPath can have three forms
			//		1. External asset reference, e.g. UStaticMesh /Game/Australia/StaticMeshes/MegaScans/Nature_Rock_vbhtdixga/vbhtdixga_LOD0.vbhtdixga_LOD0
			//		2. Subobject reference, e.g. UStaticMeshActor::StaticMeshComponent /Game/MapName.MapName:PersistentLevel.StaticMeshActor_42.StaticMeshComponent
			//		3. World reference, similar structure as subobject reference but the object is unowned object.
			const FSoftObjectPath ObjectPath = ObjectInfo.ReferencedObjects[ObjectIndex];

			UObject* OriginalReferenceValue = ObjectPath.ResolveObject();
			if (!OriginalReferenceValue)
			{
				OriginalReferenceValue = ObjectPath.TryLoad();
			}

			Object = OriginalReferenceValue;
		}
		else if (bOriginalValueWasNull)
		{
			Object = nullptr;
		}
		else
		{
			checkf(false, TEXT("Data appears to be corrupted. Index %d is not a valid index to the ReferencedObjects array and the object was not marked as null reference during saving."), ObjectIndex)
			SetError();
		}

	}
	else
	{
		FBaseObjectInfo& ObjectInfo = *ObjectInfoUnion.ObjectInfo;

		int32 ObjectIndex = INDEX_NONE;
		if (Object)
		{
			ObjectIndex = ObjectInfo.ReferencedObjects.AddUnique(Object);
		}

		// Track this object index as part of this property
		GetProperty().AddObjectReference(Offset, ObjectIndex);

		*this << ObjectIndex;
	}

	return *this;
}

FLevelSnapshot_Property& FObjectSnapshotArchive::GetProperty()
{
	FBaseObjectInfo& ObjectInfo = *ObjectInfoUnion.ObjectInfo;
	FPropertyInfo PropInfo = BuildPropertyInfo();

	if (FLevelSnapshot_Property* PropertySnapshot = ObjectInfo.Properties.Find(PropInfo.PropertyPath))
	{
		return *PropertySnapshot;
	}
	
	return ObjectInfo.Properties.Add(MoveTemp(PropInfo.PropertyPath), FLevelSnapshot_Property(PropInfo.Property, PropInfo.PropertyDepth));
}

FObjectSnapshotArchive::FPropertyInfo FObjectSnapshotArchive::BuildPropertyInfo()
{
	// if we have a property chain build the property info out of it
	const FArchiveSerializedPropertyChain* PropertyChain = GetSerializedPropertyChain();
	if (PropertyChain && PropertyChain->GetNumProperties() > 0)
	{
		return { PropertyChain->GetPropertyFromRoot(0)->GetFName(), PropertyChain->GetPropertyFromRoot(0), 0 };
	}
	// Otherwise, if we are in a non property block, return a PropertyInfo representing the non property data
	return { FName(TEXT("PrePropertyBlock")) , nullptr, (uint32)-1 };
}

const FLevelSnapshot_Property* FObjectSnapshotArchive::FindMatchingProperty() const
{
	const FBaseObjectInfo& ObjectInfo = *ObjectInfoUnion.ConstObjectInfo;

	for (const TPair<FName, FLevelSnapshot_Property>& PropertyEntry : ObjectInfo.Properties)
	{
		const FLevelSnapshot_Property& PropertySnapshot = PropertyEntry.Value;

		if (PropertySnapshot.DataOffset == Offset)
		{
			return &PropertySnapshot;
		}
	}

	return nullptr;
}