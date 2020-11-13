// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActorSnapshot.h"

#include "HAL/UnrealMemory.h"
#include "LevelSnapshot.h"
#include "LevelSnapshotFilters.h"
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

class FActorSnapshotReader : public FArchiveUObject
{
	// Internal Helper class
	struct FPropertyInfo
	{
		FName PropertyPath;
		FProperty* Property;
		uint32 PropertyDepth;
	};

public:
	FActorSnapshotReader(const FBaseObjectInfo& InObjectInfo, const ULevelSnapshotFilter* InFilter = nullptr)
		: ObjectInfo(InObjectInfo)
		, Offset(0)
		, Filter(InFilter)
	{
		this->SetWantBinaryPropertySerialization(false);
		this->SetIsLoading(true);
		this->SetIsTransacting(true);
	}

	virtual FString GetArchiveName() const override
	{
		return TEXT("FActorSnapshotReader");
	}

	virtual int64 TotalSize() override
	{
		return ObjectInfo.SerializedData.Data.Num();
	}

	virtual int64 Tell() override
	{
		return Offset;
	}

	virtual void Seek(int64 InPos) override
	{
		checkSlow(Offset <= ObjectInfo.SerializedData.Data.Num());
		Offset = InPos;
	}

	virtual bool ShouldSkipProperty(const FProperty* InProperty) const override
	{
		bool bShouldSkipProperty = FArchiveUObject::ShouldSkipProperty(InProperty);

		if (!bShouldSkipProperty && Filter)
		{
			FSoftClassPath SoftClassPath(ObjectInfo.ObjectClassPathName);
			UClass* ActorClass = SoftClassPath.ResolveClass();

			FString PropertyName = InProperty->GetName();

			bShouldSkipProperty = !Filter->IsPropertyValid(ObjectInfo.ObjectName, ActorClass, PropertyName);
		}

		return bShouldSkipProperty;
	}

private:

	virtual void Serialize(void* Data, int64 Num) override
	{
		if (Num)
		{
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
	}

	const FLevelSnapshot_Property* FindMatchingProperty() const
	{
		for (auto& PropertyEntry : ObjectInfo.Properties)
		{
			const FLevelSnapshot_Property& PropertySnapshot = PropertyEntry.Value;

			if (PropertySnapshot.DataOffset == Offset)
			{
				return &PropertySnapshot;
			}
		}

		return nullptr;
	};

	FArchive& operator<<(class FName& Name) override
	{
		int32 NameIndex;

		(FArchive&)*this << NameIndex;

		if (ObjectInfo.ReferencedNames.IsValidIndex(NameIndex))
		{
			Name = ObjectInfo.ReferencedNames[NameIndex];
		}
		else
		{
			SetError();
		}

		return (FArchive&)*this;
	}
	FArchive& operator<<(class UObject*& Object) override
	{
		int32 ObjectIndex;

		(FArchive&)*this << ObjectIndex;

		if (ObjectInfo.ReferencedObjects.IsValidIndex(ObjectIndex))
		{
			const FSoftObjectPath ObjectPath = ObjectInfo.ReferencedObjects[ObjectIndex];
			Object = ObjectPath.ResolveObject();

			if (!Object)
			{
				//UE_LOG(LogTemp, Warning, TEXT("Unable to resolve Referenced Object \"%s\" so trying to load it instead."), *ObjectPath.ToString());
				Object = ObjectPath.TryLoad();
			}
		}
		else
		{
			SetError();
		}

		return (FArchive&)*this;
	}

	const FBaseObjectInfo& ObjectInfo;
	int64 Offset;
	const ULevelSnapshotFilter* Filter;
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
	FActorSnapshotWriter(FBaseObjectInfo& InObjectInfo)
		: ObjectInfo(InObjectInfo)
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
		checkSlow(Offset <= ObjectInfo.SerializedData.Data.Num());
		Offset = InPos;
	}

private:
	virtual void Serialize(void* SerData, int64 Num) override
	{
		if (Num)
		{
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
			FMemory::Memcpy(&ObjectInfo.SerializedData.Data[(int32)Offset], SerData, Num);
			Offset += Num;
		}
	}

	FLevelSnapshot_Property& GetProperty()
	{
		FPropertyInfo PropInfo = BuildPropertyInfo();

		if (FLevelSnapshot_Property* PropertySnapshot = ObjectInfo.Properties.Find(PropInfo.PropertyPath))
		{
			return *PropertySnapshot;
		}
		else
		{
			return ObjectInfo.Properties.Add(MoveTemp(PropInfo.PropertyPath), FLevelSnapshot_Property(PropInfo.Property, PropInfo.PropertyDepth));
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
		int32 NameIndex = ObjectInfo.ReferencedNames.AddUnique(N);

		// Track this name index as part of this property
		GetProperty().AddNameReference(Offset, NameIndex);

		return (FArchive&)*this << NameIndex;
	}
	FArchive& operator<<(class UObject*& Res) override
	{
		int32 ObjectIndex = INDEX_NONE;
		if (Res)
		{
			if (AActor* TargetActor = Cast<AActor>((UObject*)ObjectInfo.ObjectAddress))
			{
				if (Res->IsInOuter(TargetActor))
				{
					UE_LOG(LogTemp, Warning, TEXT("Referenced Object is owned by the target actor: %s"), *Res->GetName());
				}
			}
			ObjectIndex = ObjectInfo.ReferencedObjects.AddUnique(Res);
		}

		// Track this object index as part of this property
		GetProperty().AddObjectReference(Offset, ObjectIndex);

		return (FArchive&)*this << ObjectIndex;
	}

	FBaseObjectInfo& ObjectInfo;
	int64 Offset;
};


FLevelSnapshot_Actor::FLevelSnapshot_Actor(AActor* TargetActor)
	: Base(TargetActor)
{
	FActorSnapshotWriter Writer(Base);

	TargetActor->Serialize(Writer);

	TArray<UActorComponent*> Components;
	TargetActor->GetComponents<UActorComponent>(Components);

	for (UActorComponent* Component : Components)
	{
		if (Component)
		{
			ComponentSnapshots.Add(Component->GetPathName(), FLevelSnapshot_Component(Component));
		}
	}
}

AActor* FLevelSnapshot_Actor::GetDeserializedActor() const
{
	AActor* DeserializedActor = nullptr;

	FSoftClassPath SoftClassPath(Base.ObjectClassPathName);

	if (UClass* TargetClass = SoftClassPath.ResolveClass())
	{
		//void* DeserializationBuffer = FMemory::Malloc(TargetClass->GetStructureSize());

		DeserializedActor = NewObject<AActor>(GetTransientPackage(), TargetClass, NAME_None, EObjectFlags::RF_Transient);
		
		Deserialize(DeserializedActor);
	}

	return DeserializedActor;;
}

void FLevelSnapshot_Actor::Deserialize(AActor* TargetActor) const
{
	if (!TargetActor)
	{
		return;
	}

#if WITH_EDITOR
	TargetActor->Modify(true);
#endif

	FActorSnapshotReader Reader(Base);

	TargetActor->Serialize(Reader);

	// Deserialize all the components

	TArray<UActorComponent*> Components;
	TargetActor->GetComponents<UActorComponent>(Components);

	for (UActorComponent* Component : Components)
	{
		if (const FLevelSnapshot_Component* ComponentSnapshot = ComponentSnapshots.Find(Component->GetPathName()))
		{
			UE_LOG(LogTemp, Warning, TEXT("Deserializing Component: %s"), *ComponentSnapshot->Base.ObjectName.ToString());

			FActorSnapshotReader ComponentReader(ComponentSnapshot->Base);

			Component->Serialize(ComponentReader);
		}
	}

	TargetActor->UpdateComponentTransforms();
	TargetActor->PostLoad();
}

FLevelSnapshot_Component::FLevelSnapshot_Component(UActorComponent* TargetComponent)
	: Base(TargetComponent)
{
	if (USceneComponent* SceneComponent = Cast<USceneComponent>(TargetComponent))
	{
		if (USceneComponent* ParentComponent = SceneComponent->GetAttachParent())
		{
			ParentComponentPath = ParentComponent->GetPathName();
			bIsSceneComponent = true;
		}
	}

	FActorSnapshotWriter Writer(Base);

	TargetComponent->Serialize(Writer);
}
