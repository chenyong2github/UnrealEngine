// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActorSnapshotData.h"

#include "ApplySnapshotDataArchiveV2.h"
#include "LevelSnapshotSelections.h"
#include "LevelSnapshotsLog.h"
#include "PropertySelectionMap.h"
#include "TakeWorldObjectSnapshotArchive.h"
#include "WorldSnapshotData.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/Package.h"

FActorSnapshotData FActorSnapshotData::SnapshotActor(AActor* OriginalActor, FWorldSnapshotData& WorldData)
{
	FActorSnapshotData Result;
	
	UClass* ActorClass = OriginalActor->GetClass();
	Result.ActorClass = ActorClass;
	
	FTakeWorldObjectSnapshotArchive Serializer = FTakeWorldObjectSnapshotArchive::MakeArchiveForSavingWorldObject(Result.SerializedActorData, WorldData, OriginalActor);
	OriginalActor->Serialize(Serializer);
	WorldData.AddClassDefault(OriginalActor->GetClass());
	
	TInlineComponentArray<UActorComponent*> Components;
	OriginalActor->GetComponents(Components);
	for (UActorComponent* Comp : Components)
	{
		TOptional<FComponentSnapshotData> SerializedComponentData = FComponentSnapshotData::SnapshotComponent(Comp, WorldData);
		if (SerializedComponentData)
		{
			const int32 ComponentIndex = WorldData.AddSubobjectDependency(Comp);
			Result.ComponentData.Add(ComponentIndex, *SerializedComponentData);
		}
	}
	
	return Result;
}

void FActorSnapshotData::DeserializeIntoExistingWorldActor(UWorld* SnapshotWorld, AActor* OriginalActor, FWorldSnapshotData& WorldData, UPackage* InLocalisationSnapshotPackage, const FPropertySelectionMap& SelectedProperties)
{
	auto DeserializeActor = [this, &WorldData, &SelectedProperties](AActor* OriginalActor, AActor* DeserializedActor)
	{
		const FPropertySelection* ActorPropertySelection = SelectedProperties.GetSelectedProperties(OriginalActor);
		if (ActorPropertySelection)
		{
#if WITH_EDITOR
			OriginalActor->Modify();
#endif
			FApplySnapshotDataArchiveV2::ApplyToExistingEditorWorldObject(SerializedActorData, WorldData, OriginalActor, DeserializedActor, SelectedProperties, *ActorPropertySelection);
		}
	};
	auto DeserializeComponent = [&SelectedProperties, &WorldData](FObjectSnapshotData& SerializedCompData, FComponentSnapshotData& CompData, UActorComponent* Original, UActorComponent* Deserialized)
	{
		const FPropertySelection* ComponentSelectedProperties = SelectedProperties.GetSelectedProperties(Original);
		if (ComponentSelectedProperties)
		{		
#if WITH_EDITOR
			Original->Modify();
#endif
			FApplySnapshotDataArchiveV2::ApplyToExistingEditorWorldObject(SerializedCompData, WorldData, Original, Deserialized, SelectedProperties, *ComponentSelectedProperties);
		};
	};

	DeserializeIntoWorldActor(SnapshotWorld, OriginalActor, WorldData, InLocalisationSnapshotPackage, DeserializeActor, DeserializeComponent);
}

void FActorSnapshotData::DeserializeIntoRecreatedEditorWorldActor(UWorld* SnapshotWorld, AActor* OriginalActor, FWorldSnapshotData& WorldData, UPackage* InLocalisationSnapshotPackage, const FPropertySelectionMap& SelectedProperties)
{
	auto DeserializeActor = [this, &WorldData, &SelectedProperties](AActor* OriginalActor, AActor* DeserializedActor)
	{
#if WITH_EDITOR
		OriginalActor->Modify();
#endif
		FApplySnapshotDataArchiveV2::ApplyToRecreatedEditorWorldObject(SerializedActorData, WorldData, OriginalActor, DeserializedActor, SelectedProperties);
	};
	auto DeserializeComponent = [&WorldData, &SelectedProperties](FObjectSnapshotData& SerializedCompData, FComponentSnapshotData& CompData, UActorComponent* Original, UActorComponent* Deserialized)
	{
#if WITH_EDITOR
		Original->Modify();
#endif
		FApplySnapshotDataArchiveV2::ApplyToRecreatedEditorWorldObject(SerializedCompData, WorldData, Original, Deserialized, SelectedProperties); 
	};
	
	DeserializeIntoWorldActor(SnapshotWorld, OriginalActor, WorldData, InLocalisationSnapshotPackage, DeserializeActor, DeserializeComponent);
}

TOptional<AActor*> FActorSnapshotData::GetPreallocatedIfValidButDoNotAllocate() const
{
	return CachedSnapshotActor.IsValid() ? CachedSnapshotActor.Get() : TOptional<AActor*>();
}

TOptional<AActor*> FActorSnapshotData::GetPreallocated(UWorld* SnapshotWorld, FWorldSnapshotData& WorldData) const
{
	if (!CachedSnapshotActor.IsValid())
	{
		const FSoftClassPath SoftClassPath(ActorClass);
		UClass* TargetClass = SoftClassPath.ResolveClass();
		if (!ensureAlways(TargetClass))
		{
			UE_LOG(LogLevelSnapshots, Error, TEXT("Unknown class %s. The snapshot is mostly likely referencing a class that was deleted."), *SoftClassPath.ToString());
			return {};
		}
		
		// TODO: Maybe there is a faster way than calling SpawnActor...
		FActorSpawnParameters SpawnParams;
		SpawnParams.Template = Cast<AActor>(WorldData.GetClassDefault(TargetClass));
		SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
		if (ensureMsgf(SpawnParams.Template, TEXT("Failed to class default. This should not happen. Investigate.")))
		{
			// We're passing in SpawnParams.Template->GetClass() instead of TargetClass:
				// When you recompile a Blueprint, it creates a new REINST class.
				// This would cause the SpawnParams.Template to have a different class than TargetClass: that would cause SpawnActor to fail.
			UClass* ClassToUse = SpawnParams.Template->GetClass();
			SpawnParams.Name = *FString("SnapshotObjectInstance_").Append(*MakeUniqueObjectName(SnapshotWorld, ClassToUse).ToString());
			CachedSnapshotActor = SnapshotWorld->SpawnActor<AActor>(ClassToUse, SpawnParams);
		}
		else
		{
			CachedSnapshotActor = SnapshotWorld->SpawnActor<AActor>(TargetClass, SpawnParams);
		}
	}

	ensureAlwaysMsgf(CachedSnapshotActor.IsValid(), TEXT("Failed to spawn actor of class '%s'"), *ActorClass.ToString());
	return CachedSnapshotActor.IsValid() ? TOptional<AActor*>(CachedSnapshotActor.Get()) : TOptional<AActor*>();
}

TOptional<AActor*> FActorSnapshotData::GetDeserialized(UWorld* SnapshotWorld, FWorldSnapshotData& WorldData, UPackage* InLocalisationSnapshotPackage)
{
	if (bReceivedSerialisation && CachedSnapshotActor.IsValid())
	{
		return CachedSnapshotActor.Get();
	}

	const TOptional<AActor*> Preallocated = GetPreallocated(SnapshotWorld, WorldData);
	if (!Preallocated)
	{
		return {};
	}
	bReceivedSerialisation = true;
	
	AActor* PreallocatedActor = Preallocated.GetValue();
	FSnapshotArchive::RestoreData(SerializedActorData, WorldData, PreallocatedActor, InLocalisationSnapshotPackage);

	DeserializeComponents(PreallocatedActor, WorldData, [InLocalisationSnapshotPackage](FObjectSnapshotData& SerializedCompData, FComponentSnapshotData& CompData, UActorComponent* Comp, FWorldSnapshotData& SharedData)
	{
		CompData.DeserializeIntoTransient(SerializedCompData, Comp, SharedData, InLocalisationSnapshotPackage);
	});

	PreallocatedActor->UpdateComponentTransforms();
	return Preallocated;
}

FSoftClassPath FActorSnapshotData::GetActorClass() const
{
	return ActorClass;
}

void FActorSnapshotData::DeserializeIntoWorldActor(UWorld* SnapshotWorld, AActor* OriginalActor, FWorldSnapshotData& WorldData, UPackage* InLocalisationSnapshotPackage, FSerializeActor SerializeActor, FSerializeComponent SerializeComponent)
{
	const TOptional<AActor*> Deserialized = GetDeserialized(SnapshotWorld, WorldData, InLocalisationSnapshotPackage);
	if (!Deserialized)
	{
		UE_LOG(LogLevelSnapshots, Warning, TEXT("Failed to serialize into actor %s. Skipping..."), *OriginalActor->GetName());
		return;
	}

	SerializeActor(OriginalActor, *Deserialized);

	TInlineComponentArray<UActorComponent*> DeserializedComponents;
	Deserialized.GetValue()->GetComponents(DeserializedComponents);
	DeserializeComponents(OriginalActor, WorldData, [&SerializeComponent, &DeserializedComponents](FObjectSnapshotData& SerializedCompData, FComponentSnapshotData& CompData, UActorComponent* Comp, FWorldSnapshotData& SharedData)
    {
        const FName OriginalCompName = Comp->GetFName();
        UActorComponent** DeserializedCompCounterpart = DeserializedComponents.FindByPredicate([OriginalCompName](UActorComponent* Other)
        {
            return Other->GetFName() == OriginalCompName;
        });
        if (!DeserializedCompCounterpart)
        {
            UE_LOG(LogLevelSnapshots, Warning, TEXT("Failed to find component called %s on temp deserialized snapshot actor. Skipping component..."), *OriginalCompName.ToString())
        	return;
        }
		
        SerializeComponent(SerializedCompData, CompData, Comp, *DeserializedCompCounterpart);
		
		// We may have modified render information, e.g. for lights we may have changed intensity or colour
		// It may be more efficient to track whether we actually changed render state
		Comp->MarkRenderStateDirty();
    });

	OriginalActor->UpdateComponentTransforms();
}

void FActorSnapshotData::DeserializeComponents(AActor* IntoActor, FWorldSnapshotData& WorldData, TFunction<void(FObjectSnapshotData& SerializedCompData, FComponentSnapshotData& ComponentMetaData, UActorComponent* ActorComp, FWorldSnapshotData& SharedData)>&& Callback)
{
	TInlineComponentArray<UActorComponent*> Components;
	IntoActor->GetComponents(Components);
	for (auto CompIt = ComponentData.CreateIterator(); CompIt; ++CompIt)
	{
		// Instances and construction script are not supported 
		if (!CompIt->Value.IsRestoreSupportedForSavedComponent())
		{
			continue;
		}
		
		const FSoftObjectPath& OriginalComponentPath = WorldData.SerializedObjectReferences[CompIt->Key];
		FSubobjectSnapshotData& SnapshotData = WorldData.Subobjects[CompIt->Key];
		
		const FString& SubPath = OriginalComponentPath.GetSubPathString();
		const int32 LastDot = SubPath.Find(TEXT("."), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		checkf(LastDot != INDEX_NONE, TEXT("FSoftObjectPath::SubPathString always has at least one '.' when referencing a component, e.g. ActorName.ComponentName. Data appears to be corrupted."));
		
		const FString OriginalComponentName = SubPath.RightChop(LastDot + 1); // + 1 because we don't want the '.'
		check(OriginalComponentName.Len() > 0);

		for (UActorComponent* Comp : Components)
		{
			if (Comp->GetName().Equals(OriginalComponentName))
			{
				Callback(SnapshotData, CompIt->Value, Comp, WorldData);
			}
		}
	}
}
