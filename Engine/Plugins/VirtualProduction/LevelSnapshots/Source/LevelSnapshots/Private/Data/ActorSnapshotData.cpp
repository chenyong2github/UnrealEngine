// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActorSnapshotData.h"

#include "ApplySnapshotDataArchiveV2.h"
#include "LevelSnapshotSelections.h"
#include "LevelSnapshotsLog.h"
#include "TakeWorldObjectSnapshotArchive.h"
#include "WorldSnapshotData.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"

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

void FActorSnapshotData::DeserializeIntoWorldActor(UWorld* SnapshotWorld, AActor* OriginalActor, FWorldSnapshotData& WorldData, ULevelSnapshotSelectionSet* SelectedProperties)
{
	const TOptional<AActor*> Deserialized = GetDeserialized(SnapshotWorld, WorldData);
	if (!Deserialized)
	{
		UE_LOG(LogLevelSnapshots, Warning, TEXT("Failed to serialize into actor %s. Skipping..."), *OriginalActor->GetName());
		return;
	}
	
	const FPropertySelection* ActorPropertySelection = SelectedProperties->GetSelectedProperties(OriginalActor);
	if (ActorPropertySelection)
	{
#if WITH_EDITOR
		OriginalActor->Modify();
#endif
		FApplySnapshotDataArchiveV2::ApplyToWorldObject(SerializedActorData, WorldData, OriginalActor, *Deserialized, *ActorPropertySelection);
	}

	TInlineComponentArray<UActorComponent*> DeserializedComponents;
	Deserialized.GetValue()->GetComponents(DeserializedComponents);
	DeserializeComponents(OriginalActor, WorldData, [SelectedProperties, &DeserializedComponents](FObjectSnapshotData& SerializedCompData, FComponentSnapshotData& CompData, UActorComponent* Comp, FWorldSnapshotData& SharedData)
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
		
		const FPropertySelection* ComponentSelectedProperties = SelectedProperties->GetSelectedProperties(Comp);
		if (ComponentSelectedProperties)
		{		
#if WITH_EDITOR
			Comp->Modify();
#endif
			CompData.DeserializeIntoWorld(SerializedCompData, Comp, *DeserializedCompCounterpart, SharedData, *ComponentSelectedProperties);
		}
    });

	OriginalActor->UpdateComponentTransforms();
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
		ensure(SpawnParams.Template);
		CachedSnapshotActor = SnapshotWorld->SpawnActor<AActor>(TargetClass);
	}
	return CachedSnapshotActor.IsValid() ? TOptional<AActor*>(CachedSnapshotActor.Get()) : TOptional<AActor*>();
}

TOptional<AActor*> FActorSnapshotData::GetDeserialized(UWorld* SnapshotWorld, FWorldSnapshotData& WorldData)
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
	FSnapshotArchive Serializer = FSnapshotArchive::MakeArchiveForRestoring(SerializedActorData, WorldData);
	PreallocatedActor->Serialize(Serializer);

	DeserializeComponents(PreallocatedActor, WorldData, [](FObjectSnapshotData& SerializedCompData, FComponentSnapshotData& CompData, UActorComponent* Comp, FWorldSnapshotData& SharedData)
	{
		CompData.DeserializeIntoTransient(SerializedCompData, Comp, SharedData);
	});

	PreallocatedActor->UpdateComponentTransforms();
	return Preallocated;
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
