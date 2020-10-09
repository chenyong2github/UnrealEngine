// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActorSnapshot.h"

#include "Serialization/ArchiveSerializedPropertyChain.h"

bool FSerializedActorData::Serialize(FArchive& Ar) 
{
	int32 Num = Data.Num();
	Ar << Num;
	if (Ar.IsLoading())
	{
		Data.AddUninitialized(Num);
		Ar.Serialize(Data.GetData(), Num);
	}
	else if (Ar.IsSaving())
	{
		Ar.Serialize(Data.GetData(), Num);
	}
	return true;
};

class FActorSnapshotWriter : public FArchiveUObject
{
	// Internal Helper class
	struct FPropertyInfo
	{
		FName PropertyPath;
		FProperty* Property;
		uint32 PropertyDepth;
	};

public:
	FActorSnapshotWriter(FActorSnapshot& InSnapshot)
		: Snapshot(InSnapshot)
		, Offset(0)
	{
		this->SetWantBinaryPropertySerialization(false);
		this->SetIsSaving(true);
		this->SetIsTransacting(true);
	}

	virtual int64 Tell() override
	{
		return Offset;
	}

	virtual void Seek(int64 InPos) override
	{
		checkSlow(Offset <= Snapshot.SerializedData.Data.Num());
		Offset = InPos;
	}

private:
	virtual void Serialize(void* SerData, int64 Num) override
	{
		if (Num)
		{
			int32 ToAlloc = Offset + (int32)Num - Snapshot.SerializedData.Data.Num();
			if (ToAlloc > 0)
			{
				Snapshot.SerializedData.Data.AddUninitialized(ToAlloc);

				FInternalPropertySnapshot& Property = GetProperty();

				// if we are in a property block
				if (!Property.PropertyPath.Get())
				{
					// Track this property offset in the serialized data, if actually allocated for it, and this isn't a seek back in the stream
					Property.AppendSerializedData((uint32)Offset, (uint32)ToAlloc);

					// Map the start and end of the property block
					Snapshot.PropertyBlockStart = FMath::Min(Snapshot.PropertyBlockStart, (uint32)Offset);
					Snapshot.PropertyBlockEnd = FMath::Max(Snapshot.PropertyBlockEnd, (uint32)(Offset + ToAlloc));
				}
			}
			FMemory::Memcpy(&Snapshot.SerializedData.Data[(int32)Offset], SerData, Num);
			Offset += Num;
		}
	}

	FInternalPropertySnapshot& GetProperty()
	{
		FPropertyInfo PropInfo = BuildPropertyInfo();
		if (FInternalPropertySnapshot* PropertySnapshot = Snapshot.Properties.Find(PropInfo.PropertyPath))
		{
			return *PropertySnapshot;
		}
		else
		{
			return Snapshot.Properties.Add(MoveTemp(PropInfo.PropertyPath), FInternalPropertySnapshot(PropInfo.Property, PropInfo.PropertyDepth));
		}
	}

	FPropertyInfo BuildPropertyInfo()
	{
		// if we have a property chain build the property info out of it
		const FArchiveSerializedPropertyChain* PropertyChain = GetSerializedPropertyChain();
		if (PropertyChain && PropertyChain->GetNumProperties() > 0)
		{
			return { PropertyChain->GetPropertyFromRoot(0)->GetFName(), PropertyChain->GetPropertyFromRoot(0), 0 };
		}
		// Otherwise, if we are in a non property block, return a PropertyInfo representing the non property data
		else
		{
			return { FName(TEXT("PrePropertyBlock")) , nullptr, (uint32)-1 };
		}
	}

	FArchive& operator<<(class FName& N) override
	{
		int32 NameIndex = Snapshot.ReferencedNames.AddUnique(N);

		// Track this name index as part of this property
		GetProperty().AddNameReference(Offset, NameIndex);

		return (FArchive&)*this << NameIndex;
	}
	FArchive& operator<<(class UObject*& Res) override
	{
		int32 ObjectIndex = INDEX_NONE;
		if (Res)
		{
			ObjectIndex = Snapshot.ReferencedObjects.Add(Res);
		}

		// Track this object index as part of this property
		GetProperty().AddObjectReference(Offset, ObjectIndex);

		return (FArchive&)*this << ObjectIndex;
	}

	FActorSnapshot& Snapshot;
	int64 Offset;
};


FActorSnapshot::FActorSnapshot(AActor* TargetActor)
	: ObjectName(TargetActor ? TargetActor->GetFName() : FName())
	, ObjectOuterPathName(TargetActor&& TargetActor->GetOuter() ? TargetActor->GetOuter()->GetPathName() : FString()) // TODO: can optimize GetPathName?
	, ObjectClassPathName(TargetActor ? TargetActor->GetClass()->GetPathName() : FString())
	, ObjectFlags(TargetActor ? (uint32)TargetActor->GetFlags() : 0)
	, InternalObjectFlags(TargetActor ? (uint32)TargetActor->GetInternalFlags() : 0)
	, ObjectAddress((uint64)TargetActor)
	, InternalIndex(TargetActor ? TargetActor->GetUniqueID() : 0)
	, PropertyBlockStart(0)
	, PropertyBlockEnd(0)
{
	FActorSnapshotWriter Writer(*this);

	TargetActor->Serialize(Writer);
}
