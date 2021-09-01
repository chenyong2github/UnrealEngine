// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActorSnapshotData.h"

#include "ApplySnapshotDataArchiveV2.h"
#include "LevelSnapshotsLog.h"
#include "PropertySelectionMap.h"
#include "TakeWorldObjectSnapshotArchive.h"
#include "WorldSnapshotData.h"
#include "CustomSerialization/CustomObjectSerializationWrapper.h"
#include "CustomSerialization/CustomSerializationDataManager.h"

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
	// If external modules registered for custom serialisation, trigger their callbacks
	FCustomObjectSerializationWrapper::TakeSnapshotForActor(OriginalActor, Result.CustomActorSerializationData, WorldData);
	
	TInlineComponentArray<UActorComponent*> Components;
	OriginalActor->GetComponents(Components);
	for (UActorComponent* Comp : Components)
	{
		TOptional<FComponentSnapshotData> SerializedComponentData = FComponentSnapshotData::SnapshotComponent(Comp, WorldData);
		if (SerializedComponentData)
		{
			const int32 ComponentIndex = WorldData.AddSubobjectDependency(Comp);
			Result.ComponentData.Add(ComponentIndex, *SerializedComponentData);
			// If external modules registered for custom serialisation, trigger their callbacks
			FCustomObjectSerializationWrapper::TakeSnapshotForSubobject(Comp, WorldData);
		}
	}
	
	return Result;
}

void FActorSnapshotData::DeserializeIntoExistingWorldActor(UWorld* SnapshotWorld, AActor* OriginalActor, FWorldSnapshotData& WorldData, UPackage* InLocalisationSnapshotPackage, const FPropertySelectionMap& SelectedProperties)
{
	auto DeserializeActor = [this, &WorldData, InLocalisationSnapshotPackage, &SelectedProperties](AActor* OriginalActor, AActor* DeserializedActor)
	{
		const FRestoreObjectScope FinishRestore = FCustomObjectSerializationWrapper::PreActorRestore_EditorWorld(OriginalActor, CustomActorSerializationData, WorldData, SelectedProperties, InLocalisationSnapshotPackage);
		const FPropertySelection* ActorPropertySelection = SelectedProperties.GetSelectedProperties(OriginalActor);
		if (ActorPropertySelection)
		{
			FApplySnapshotDataArchiveV2::ApplyToExistingEditorWorldObject(SerializedActorData, WorldData, OriginalActor, DeserializedActor, SelectedProperties, *ActorPropertySelection);
		}
	};
	auto DeserializeComponent = [OriginalActor, InLocalisationSnapshotPackage, &SelectedProperties, &WorldData](FObjectSnapshotData& SerializedCompData, FComponentSnapshotData& CompData, UActorComponent* Original, UActorComponent* Deserialized)
	{
		const FRestoreObjectScope FinishRestore = FCustomObjectSerializationWrapper::PreSubobjectRestore_EditorWorld(Deserialized, Original, WorldData, SelectedProperties, InLocalisationSnapshotPackage);
		const FPropertySelection* ComponentSelectedProperties = SelectedProperties.GetSelectedProperties(Original);
		if (ComponentSelectedProperties)
		{		
			FApplySnapshotDataArchiveV2::ApplyToExistingEditorWorldObject(SerializedCompData, WorldData, Original, Deserialized, SelectedProperties, *ComponentSelectedProperties);
		};
	};

	DeserializeIntoWorldActor(SnapshotWorld, OriginalActor, WorldData, InLocalisationSnapshotPackage, DeserializeActor, DeserializeComponent);
}

void FActorSnapshotData::DeserializeIntoRecreatedEditorWorldActor(UWorld* SnapshotWorld, AActor* OriginalActor, FWorldSnapshotData& WorldData, UPackage* InLocalisationSnapshotPackage, const FPropertySelectionMap& SelectedProperties)
{
	auto DeserializeActor = [this, &WorldData, InLocalisationSnapshotPackage, &SelectedProperties](AActor* OriginalActor, AActor* DeserializedActor)
	{
		const FRestoreObjectScope FinishRestore = FCustomObjectSerializationWrapper::PreActorRestore_EditorWorld(OriginalActor, CustomActorSerializationData, WorldData, SelectedProperties, InLocalisationSnapshotPackage);
		FApplySnapshotDataArchiveV2::ApplyToRecreatedEditorWorldObject(SerializedActorData, WorldData, OriginalActor, DeserializedActor, SelectedProperties);
	};
	auto DeserializeComponent = [OriginalActor, InLocalisationSnapshotPackage, &WorldData, &SelectedProperties](FObjectSnapshotData& SerializedCompData, FComponentSnapshotData& CompData, UActorComponent* Original, UActorComponent* Deserialized)
	{
		const FRestoreObjectScope FinishRestore = FCustomObjectSerializationWrapper::PreSubobjectRestore_EditorWorld(Deserialized, Original, WorldData, SelectedProperties, InLocalisationSnapshotPackage);
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
		UClass* TargetClass = SoftClassPath.TryLoadClass<AActor>();
		if (!TargetClass)
		{
			UE_LOG(LogLevelSnapshots, Error, TEXT("Unknown class %s. The snapshot is mostly likely referencing a class that was deleted."), *SoftClassPath.ToString());
			return {};
		}
		
		// TODO: Maybe there is a faster way than calling SpawnActor...
		FActorSpawnParameters SpawnParams;
		SpawnParams.Template = Cast<AActor>(WorldData.GetClassDefault(TargetClass));
		SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
		if (ensureMsgf(SpawnParams.Template, TEXT("Failed to get class default. This should not happen. Investigate.")))
		{
			UClass* ClassToUse = SpawnParams.Template->GetClass();
			SpawnParams.Name = *FString("SnapshotObjectInstance_").Append(*MakeUniqueObjectName(SnapshotWorld, ClassToUse).ToString());
			CachedSnapshotActor = SnapshotWorld->SpawnActor<AActor>(ClassToUse, SpawnParams);
		}
		else
		{
			CachedSnapshotActor = SnapshotWorld->SpawnActor<AActor>(TargetClass, SpawnParams);
		}
	}

#if WITH_EDITOR
	// Hide this actor so external systems can see that this components should not render, i.e. make USceneComponent::ShouldRender return false
	if (ensureMsgf(CachedSnapshotActor.IsValid(), TEXT("Failed to spawn actor of class '%s'"), *ActorClass.ToString()))
	{
		CachedSnapshotActor->SetIsTemporarilyHiddenInEditor(true);
	}
#endif
	
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
	{
		const FRestoreObjectScope FinishRestore = FCustomObjectSerializationWrapper::PreActorRestore_SnapshotWorld(PreallocatedActor, CustomActorSerializationData, WorldData, InLocalisationSnapshotPackage);
		FSnapshotArchive::ApplyToSnapshotWorldObject(SerializedActorData, WorldData, PreallocatedActor, InLocalisationSnapshotPackage);
	}

	DeserializeComponents(PreallocatedActor, WorldData,
		[&WorldData, InLocalisationSnapshotPackage](
			FObjectSnapshotData& SerializedCompData,
			FComponentSnapshotData& CompData,
			UActorComponent* Comp,
			const FSoftObjectPath& OriginalComponentPath,
			FWorldSnapshotData& SharedData)
		{
			const FRestoreObjectScope FinishRestore = FCustomObjectSerializationWrapper::PreSubobjectRestore_SnapshotWorld(Comp, OriginalComponentPath, WorldData, InLocalisationSnapshotPackage);
			CompData.DeserializeIntoTransient(SerializedCompData, Comp, SharedData, InLocalisationSnapshotPackage);
		}
	);

	PreallocatedActor->UpdateComponentTransforms();
#if WITH_EDITOR
	// Hide this actor so external systems can see that this components should not render, i.e. make USceneComponent::ShouldRender return false
	if (!ensureMsgf(PreallocatedActor->IsTemporarilyHiddenInEditor(), TEXT("Transient property bHiddenEdTemporary was set to false by serializer. This should not happen. Investigate.")))
	{
		CachedSnapshotActor->SetIsTemporarilyHiddenInEditor(true);
	}
#endif
	
	return Preallocated;
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
	DeserializeComponents(OriginalActor, WorldData,
		[&SerializeComponent, &DeserializedComponents](
			FObjectSnapshotData& SerializedCompData,
			FComponentSnapshotData& CompData,
			UActorComponent* Comp,
			const FSoftObjectPath& OriginalComponenPath,
			FWorldSnapshotData& SharedData
			)
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

void FActorSnapshotData::DeserializeComponents(
	AActor* IntoActor,
	FWorldSnapshotData& WorldData,
	FHandleFoundComponent Callback
	)
{
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

		// Serializing into component causes PostEditChange to regenerate all Blueprint generated components
		// Hence we need to obtain a new list of components every loop
		TInlineComponentArray<UActorComponent*> Components;
		IntoActor->GetComponents(Components);
		for (UActorComponent* Comp : Components)
		{
			if (Comp->GetName().Equals(OriginalComponentName))
			{
				Callback(SnapshotData, CompIt->Value, Comp, OriginalComponentPath, WorldData);
				break;
			}
		}
	}
}
