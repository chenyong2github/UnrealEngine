// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/ActorSnapshotData.h"

#include "Archive/ApplySnapshotDataArchiveV2.h"
#include "Archive/TakeWorldObjectSnapshotArchive.h"
#include "Data/WorldSnapshotData.h"
#include "Data/SnapshotCustomVersion.h"
#include "CustomSerialization/CustomObjectSerializationWrapper.h"
#include "LevelSnapshotsLog.h"
#include "LevelSnapshotsModule.h"
#include "PropertySelectionMap.h"
#include "Restorability/SnapshotRestorability.h"
#include "Util/SnapshotObjectUtil.h"
#include "Util/SnapshotUtil.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Modules/ModuleManager.h"
#include "RestorationEvents/ApplySnapshotToActorScope.h"
#include "RestorationEvents/RecreateComponentScope.h"
#include "RestorationEvents/RemoveComponentScope.h"
#include "Templates/NonNullPointer.h"
#include "UObject/Package.h"
#include "UObject/Script.h"

#if USE_STABLE_LOCALIZATION_KEYS
#include "Internationalization/TextPackageNamespaceUtil.h"
#endif

#if WITH_EDITOR
#include "Editor/UnrealEdEngine.h"
#include "LevelEditor.h"
#endif

namespace
{	
	void FindOrRecreateSavedComponents(const FActorSnapshotData& SnapshotData, FWorldSnapshotData& WorldData, AActor* Actor)
	{
		TInlineComponentArray<UActorComponent*> DefaultSnapshotComps(Actor);
		for (auto CompIt = SnapshotData.ComponentData.CreateConstIterator(); CompIt; ++CompIt)
		{
			const int32 ReferenceIndex = CompIt->Key;
			FSubobjectSnapshotData* SubobjectData = WorldData.Subobjects.Find(ReferenceIndex); 
			if (!ensure(SubobjectData))
			{
				continue;
			}
			
			const FSoftObjectPath& ComponentPath = WorldData.SerializedObjectReferences[ReferenceIndex];
			const FString ComponentName = SnapshotUtil::ExtractLastSubobjectName(ComponentPath);
			UActorComponent* const* PossibleCounterpart = DefaultSnapshotComps.FindByPredicate([&ComponentName](UActorComponent* Component)
			{
				return Component->GetName() == ComponentName;
			});
			
			UActorComponent* CounterpartComponent = PossibleCounterpart ? *PossibleCounterpart : nullptr;
			if (!CounterpartComponent)
			{
				CounterpartComponent = NewObject<UActorComponent>(Actor, SubobjectData->Class.ResolveClass(), FName(*ComponentName));
				SubobjectData->SnapshotObject = CounterpartComponent;
			}
			
			// UActorComponent::PostInitProperties implicitly calls AddOwnedComponent but we have to manually add it to the other arrays
			CounterpartComponent->CreationMethod = CompIt->Value.CreationMethod;
			switch(CounterpartComponent->CreationMethod)
			{
				case EComponentCreationMethod::Instance:
					Actor->AddInstanceComponent(CounterpartComponent);
					break;
				case EComponentCreationMethod::SimpleConstructionScript:
					Actor->BlueprintCreatedComponents.Add(CounterpartComponent);
					break;
				case EComponentCreationMethod::UserConstructionScript:
					checkf(false, TEXT("Component created in construction script currently unsupported"));
					break;
					
				case EComponentCreationMethod::Native:
				default:
					break;
			}
		}
	}

	TPair<int32, EComponentCreationMethod> FindMatchingSubobjectIndex(const FActorSnapshotData& SnapshotData, const FWorldSnapshotData& WorldData, const FString& ComponentName)
	{
		for (auto CompIt = SnapshotData.ComponentData.CreateConstIterator(); CompIt; ++CompIt)
		{
			const int32 ReferenceIndex = CompIt->Key;
			const FSoftObjectPath& SavedComponentPath = WorldData.SerializedObjectReferences[ReferenceIndex];
			const FString SavedComponentName = SnapshotUtil::ExtractLastSubobjectName(SavedComponentPath);
			if (SavedComponentName.Equals(ComponentName))
			{
				return TPair<int32, EComponentCreationMethod>(ReferenceIndex, CompIt->Value.CreationMethod);
			}
		}

		return TPair<int32, EComponentCreationMethod>(INDEX_NONE, EComponentCreationMethod::Instance);
	}

	void UpdateDetailsViewAfterUpdatingComponents()
	{
#if WITH_EDITOR
		FLevelEditorModule& LevelEditor = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
		LevelEditor.BroadcastComponentsEdited();
#endif
	}
	
	void RemovedNewComponents(const FAddedAndRemovedComponentInfo& ComponentSelection)
	{
		for (const TWeakObjectPtr<UActorComponent>& ComponentToRemove : ComponentSelection.EditorWorldComponentsToRemove)
		{
			// Evil user might have manually deleted the component between filtering and applying the snapshot
			if (ComponentToRemove.IsValid())
			{
				const FRemoveComponentScope NotifySnapshotListeners(ComponentToRemove.Get());
				ComponentToRemove->Modify();
				ComponentToRemove->DestroyComponent();
			}
		}
	}
	
	void RecreateMissingComponents(const FActorSnapshotData& SnapshotData, FWorldSnapshotData& WorldData, AActor* Actor, const FAddedAndRemovedComponentInfo& ComponentSelection, const FPropertySelectionMap& SelectionMap, UPackage* LocalisationSnapshotPackage)
	{
		struct FDeferredComponentSerializationTask
		{
			FSubobjectSnapshotData* SubobjectData;
			UActorComponent* Snapshot;
			UActorComponent* Original;

			FDeferredComponentSerializationTask(FSubobjectSnapshotData* SubobjectData, UActorComponent* Snapshot, UActorComponent* Original)
				:
				SubobjectData(SubobjectData),
				Snapshot(Snapshot),
				Original(Original)
			{}
		};
		
		// Recreate missing components
		TArray<FDeferredComponentSerializationTask> DeferredSerializationsTasks;
		for (const TWeakObjectPtr<UActorComponent>& ComponentToRestore : ComponentSelection.SnapshotComponentsToAdd)
		{
			const FName ComponentName = ComponentToRestore->GetFName();
			const TPair<int32, EComponentCreationMethod> ComponentInfo = FindMatchingSubobjectIndex(SnapshotData, WorldData, ComponentName.ToString());
			const int32 ReferenceIndex = ComponentInfo.Key;
			if (ReferenceIndex == INDEX_NONE)
			{
				continue;
			}
			FSubobjectSnapshotData* SubobjectData = WorldData.Subobjects.Find(ReferenceIndex); 
			if (!ensure(SubobjectData))
			{
				continue;
			}
			
			UClass* ComponentClass = SubobjectData->Class.TryLoadClass<UActorComponent>(); 
			const FRecreateComponentScope NotifySnapshotListeners(*SubobjectData, Actor, ComponentName, ComponentClass, ComponentInfo.Value);
			UActorComponent* Component = NewObject<UActorComponent>(Actor, ComponentClass, ComponentName);
			if (!Component)
			{
				UE_LOG(LogLevelSnapshots, Error, TEXT("Failed to recreate component called %s of class %s. Was the class removed?"), *ComponentName.ToString(), *SubobjectData->Class.ToString());
				continue;
			}
			SubobjectData->EditorObject = Component;
			
			// UActorComponent::PostInitProperties implicitly calls AddOwnedComponent but we have to manually add it to the other arrays
			Component->CreationMethod = ComponentInfo.Value;
			switch(Component->CreationMethod)
			{
			case EComponentCreationMethod::Instance:
				Actor->AddInstanceComponent(Component);
				break;
			case EComponentCreationMethod::SimpleConstructionScript:
				Actor->BlueprintCreatedComponents.Add(Component);
				break;
			case EComponentCreationMethod::UserConstructionScript:
				checkf(false, TEXT("Component created in construction script currently unsupported"));
				break;
					
			case EComponentCreationMethod::Native:
			default:
				break;
			}
			
			Component->RegisterComponent();
			// Defer task until all components allocated: components may have references to each other, e.g. AttachParent
			DeferredSerializationsTasks.Add({ SubobjectData, ComponentToRestore.Get(), Component });
		}

		// Write all saved data into the recreated component
		for (const FDeferredComponentSerializationTask& Task : DeferredSerializationsTasks)
		{
			const FRestoreObjectScope FinishRestore = FCustomObjectSerializationWrapper::PreSubobjectRestore_EditorWorld(Task.Snapshot, Task.Original, WorldData, SelectionMap, LocalisationSnapshotPackage);
			FApplySnapshotDataArchiveV2::ApplyToRecreatedEditorWorldObject(*Task.SubobjectData, WorldData, Task.Original, Task.Snapshot, SelectionMap); 
		}
	}

	void AddAndRemoveSelectedComponentsForRestore(
		const FActorSnapshotData& SnapshotData,
		FWorldSnapshotData& WorldData,
		AActor* Actor,
		const FAddedAndRemovedComponentInfo& ComponentSelection,
		const FPropertySelectionMap& SelectionMap,
		UPackage* LocalisationSnapshotPackage)
	{
		RemovedNewComponents(ComponentSelection);
		RecreateMissingComponents(SnapshotData, WorldData, Actor, ComponentSelection, SelectionMap, LocalisationSnapshotPackage);

		const bool bChangedHierarchy = ComponentSelection.SnapshotComponentsToAdd.Num() > 0 || ComponentSelection.EditorWorldComponentsToRemove.Num() > 0;
		if (bChangedHierarchy)
		{
			UpdateDetailsViewAfterUpdatingComponents();
		}
	}
}

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
		if (!FSnapshotRestorability::IsComponentDesirableForCapture(Comp))
		{
			continue;
		}
		
		TOptional<FComponentSnapshotData> SerializedComponentData = FComponentSnapshotData::SnapshotComponent(Comp, WorldData);
		if (SerializedComponentData)
		{
			const int32 ComponentIndex = SnapshotUtil::Object::AddObjectDependency(WorldData, Comp);
			Result.ComponentData.Add(ComponentIndex, *SerializedComponentData);
			// If external modules registered for custom serialisation, trigger their callbacks
			FCustomObjectSerializationWrapper::TakeSnapshotForSubobject(Comp, WorldData);
		}
	}
	
	return Result;
}

TOptional<AActor*> FActorSnapshotData::GetPreallocatedIfValidButDoNotAllocate() const
{
	return CachedSnapshotActor.IsValid() ? CachedSnapshotActor.Get() : TOptional<AActor*>();
}

TOptional<AActor*> FActorSnapshotData::GetPreallocated(UWorld* SnapshotWorld, FWorldSnapshotData& WorldData) const
{
	SCOPED_SNAPSHOT_CORE_TRACE(GetPreallocated);
	
	if (!CachedSnapshotActor.IsValid())
	{
		const FSoftClassPath SoftClassPath(ActorClass);
		UClass* TargetClass = SoftClassPath.TryLoadClass<AActor>();
		if (!TargetClass)
		{
			UE_LOG(LogLevelSnapshots, Error, TEXT("Unknown class %s. The snapshot is mostly likely referencing a class that was deleted."), *SoftClassPath.ToString());
			return {};
		}
		
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
		
		if (!ensureMsgf(CachedSnapshotActor.IsValid(), TEXT("Failed to spawn actor of class '%s'"), *ActorClass.ToString()))
		{
			return {};
		}
		
#if WITH_EDITOR
		// Hide this actor so external systems can see that this components should not render, i.e. make USceneComponent::ShouldRender return false
		CachedSnapshotActor->SetIsTemporarilyHiddenInEditor(true);
#endif
		FindOrRecreateSavedComponents(*this, WorldData, CachedSnapshotActor.Get());
	}

	return CachedSnapshotActor.IsValid() ? TOptional<AActor*>(CachedSnapshotActor.Get()) : TOptional<AActor*>();
}

TOptional<AActor*> FActorSnapshotData::GetDeserialized(UWorld* SnapshotWorld, FWorldSnapshotData& WorldData, const FSoftObjectPath& OriginalActorPath, UPackage* InLocalisationSnapshotPackage)
{
	SCOPED_SNAPSHOT_CORE_TRACE(GetDeserialized);
	
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
			FSubobjectSnapshotData& SerializedCompData,
			FComponentSnapshotData& CompData,
			UActorComponent* Comp,
			const FSoftObjectPath& OriginalComponentPath,
			FWorldSnapshotData& SharedData)
		{
			SerializedCompData.SnapshotObject = Comp;
			
			const FRestoreObjectScope FinishRestore = FCustomObjectSerializationWrapper::PreSubobjectRestore_SnapshotWorld(Comp, OriginalComponentPath, WorldData, InLocalisationSnapshotPackage);
			CompData.DeserializeIntoTransient(SerializedCompData, Comp, SharedData, InLocalisationSnapshotPackage);
		}
	);

	DeserializeSubobjectsForSnapshotActor(PreallocatedActor, WorldData, InLocalisationSnapshotPackage);
#if WITH_EDITOR
	// Hide this actor so external systems can see that this components should not render, i.e. make USceneComponent::ShouldRender return false
	if (!ensureMsgf(PreallocatedActor->IsTemporarilyHiddenInEditor(), TEXT("Transient property bHiddenEdTemporary was set to false by serializer. This should not happen. Investigate.")))
	{
		CachedSnapshotActor->SetIsTemporarilyHiddenInEditor(true);
	}
#endif

	PostSerializeSnapshotActor(PreallocatedActor, WorldData, OriginalActorPath, InLocalisationSnapshotPackage);
	
	PreallocatedActor->UpdateComponentTransforms();
	{
		// GAllowActorScriptExecutionInEditor must be temporarily true so call to UserConstructionScript isn't skipped
		FEditorScriptExecutionGuard AllowConstructionScript;
		PreallocatedActor->UserConstructionScript();
	}
	return Preallocated;
}

void FActorSnapshotData::PostSerializeSnapshotActor(AActor* SnapshotActor, FWorldSnapshotData& WorldData, const FSoftObjectPath& OriginalActorPath, UPackage* InLocalisationSnapshotPackage) const
{
	struct Local
	{
		static void RecreateRootComponentIfInstanced(AActor* OriginalActor, AActor* SnapshotActor)
		{
			USceneComponent* OriginalRoot = OriginalActor->GetRootComponent();
			
			const bool bSnapshotIsMissingRoot = SnapshotActor->GetRootComponent() == nullptr;
			// Some actors, e.g. exact instances of AActor, have no components and the editor creates an instanced root component
			const bool bSnapshotWasSavedWithoutRootComponentData = OriginalRoot && OriginalRoot->CreationMethod == EComponentCreationMethod::Instance;
			if (bSnapshotIsMissingRoot && bSnapshotWasSavedWithoutRootComponentData)
			{
				USceneComponent* SnapshotRoot = DuplicateObject<USceneComponent>(OriginalRoot, SnapshotActor, OriginalRoot->GetFName());
				UE_CLOG(SnapshotRoot == nullptr, LogLevelSnapshots, Warning, TEXT("Failed to recreate instanced root component for %s's snapshot counterpart"), *OriginalActor->GetPathName());
				if (SnapshotRoot)
				{
					SnapshotActor->SetRootComponent(
						SnapshotRoot
					);
				}
			}
		}
		
		static void HandleAttachParentNotSaved(AActor* OriginalActor, AActor* SnapshotActor, FWorldSnapshotData& WorldData, UPackage* InLocalisationSnapshotPackage)
		{
			USceneComponent* RootComponent = OriginalActor->GetRootComponent();
			// WorldSettings does not have a root component...
			if (!RootComponent)
			{
				return;
			}
			
			USceneComponent* WorldAttachParent = RootComponent->GetAttachParent();
			if (!WorldAttachParent)
			{
				return;
			}
			const TOptional<AActor*> SnapshotAttachParent = WorldData.GetDeserializedActor(WorldAttachParent->GetOwner(), InLocalisationSnapshotPackage);
			if (!SnapshotAttachParent)
			{
				return;
			}

			const TInlineComponentArray<USceneComponent*> ComponentArray(*SnapshotAttachParent);
			USceneComponent* const* SnapshotAttachComponent = ComponentArray.FindByPredicate([WorldAttachParent](USceneComponent* Comp)
			{
				return Comp->GetFName() == WorldAttachParent->GetFName();
			});

			if (SnapshotAttachComponent)
			{
				SnapshotActor->AttachToComponent(
					*SnapshotAttachComponent,
					// Do not change property values: anything other than KeepRelativeTransform will recompute
					FAttachmentTransformRules::KeepRelativeTransform,
					RootComponent->GetAttachSocketName()
					);
			}
			else
			{
				UE_LOG(LogLevelSnapshots, Warning, TEXT("Failed to fix up AttachParent for %s's snapshot counterpart"), *OriginalActor->GetPathName());
			}
		}
	};
	
	const int32 SavedVersion = WorldData.GetSnapshotVersionInfo().GetSnapshotCustomVersion();
	AActor* OriginalActor = Cast<AActor>(OriginalActorPath.ResolveObject());

	// USceneComponent::AttachParent was not yet saved.
	if (SavedVersion < FSnapshotCustomVersion::SubobjectSupport
		&& OriginalActor)
	{
		Local::RecreateRootComponentIfInstanced(OriginalActor, SnapshotActor);
		// To avoid lots of actors being unparented by accident, we set the snapshot actor's attach parent to the equivalent of the editor world
		Local::HandleAttachParentNotSaved(OriginalActor, SnapshotActor, WorldData, InLocalisationSnapshotPackage);
	}
}

void FActorSnapshotData::DeserializeIntoExistingWorldActor(UWorld* SnapshotWorld, AActor* OriginalActor, FWorldSnapshotData& WorldData, UPackage* InLocalisationSnapshotPackage, const FPropertySelectionMap& SelectedProperties)
{
	auto DeserializeActor = [this, &WorldData, InLocalisationSnapshotPackage, &SelectedProperties](AActor* OriginalActor, AActor* DeserializedActor)
	{
		const FRestoreObjectScope FinishRestore = FCustomObjectSerializationWrapper::PreActorRestore_EditorWorld(OriginalActor, CustomActorSerializationData, WorldData, SelectedProperties, InLocalisationSnapshotPackage);
		const FPropertySelection* ActorPropertySelection = SelectedProperties.GetObjectSelection(OriginalActor).GetPropertySelection();
		if (ActorPropertySelection)
		{
			FApplySnapshotDataArchiveV2::ApplyToExistingEditorWorldObject(SerializedActorData, WorldData, OriginalActor, DeserializedActor, SelectedProperties, *ActorPropertySelection);
		}
	};
	auto DeserializeComponent = [OriginalActor, InLocalisationSnapshotPackage, &SelectedProperties, &WorldData](FSubobjectSnapshotData& SerializedCompData, FComponentSnapshotData& CompData, UActorComponent* Original, UActorComponent* Deserialized)
	{
		const FRestoreObjectScope FinishRestore = FCustomObjectSerializationWrapper::PreSubobjectRestore_EditorWorld(Deserialized, Original, WorldData, SelectedProperties, InLocalisationSnapshotPackage);
		const FPropertySelection* ComponentSelectedProperties = SelectedProperties.GetObjectSelection(Original).GetPropertySelection();
		if (ComponentSelectedProperties)
		{		
			FApplySnapshotDataArchiveV2::ApplyToExistingEditorWorldObject(SerializedCompData, WorldData, Original, Deserialized, SelectedProperties, *ComponentSelectedProperties);
		};
	};

	const bool bWasRecreated = false;
	const FApplySnapshotToActorScope NotifyExternalListeners({ OriginalActor, SelectedProperties, bWasRecreated });
	
	if (const FAddedAndRemovedComponentInfo* ComponentSelection = SelectedProperties.GetObjectSelection(OriginalActor).GetComponentSelection())
	{
		AddAndRemoveSelectedComponentsForRestore(*this, WorldData, OriginalActor, *ComponentSelection, SelectedProperties, InLocalisationSnapshotPackage);
	}
	DeserializeIntoEditorWorldActor(SnapshotWorld, OriginalActor, WorldData, InLocalisationSnapshotPackage, DeserializeActor, DeserializeComponent);
}

void FActorSnapshotData::DeserializeIntoRecreatedEditorWorldActor(UWorld* SnapshotWorld, AActor* OriginalActor, FWorldSnapshotData& WorldData, UPackage* InLocalisationSnapshotPackage, const FPropertySelectionMap& SelectedProperties)
{
	auto DeserializeActor = [this, &WorldData, InLocalisationSnapshotPackage, &SelectedProperties](AActor* OriginalActor, AActor* DeserializedActor)
	{
		const FRestoreObjectScope FinishRestore = FCustomObjectSerializationWrapper::PreActorRestore_EditorWorld(OriginalActor, CustomActorSerializationData, WorldData, SelectedProperties, InLocalisationSnapshotPackage);
		FApplySnapshotDataArchiveV2::ApplyToRecreatedEditorWorldObject(SerializedActorData, WorldData, OriginalActor, DeserializedActor, SelectedProperties);
	};
	auto DeserializeComponent = [OriginalActor, InLocalisationSnapshotPackage, &WorldData, &SelectedProperties](FSubobjectSnapshotData& SerializedCompData, FComponentSnapshotData& CompData, UActorComponent* Original, UActorComponent* Deserialized)
	{
		const FRestoreObjectScope FinishRestore = FCustomObjectSerializationWrapper::PreSubobjectRestore_EditorWorld(Deserialized, Original, WorldData, SelectedProperties, InLocalisationSnapshotPackage);
		FApplySnapshotDataArchiveV2::ApplyToRecreatedEditorWorldObject(SerializedCompData, WorldData, Original, Deserialized, SelectedProperties); 
	};

	const bool bWasRecreated = true;
	const FApplySnapshotToActorScope NotifyExternalListeners({ OriginalActor, SelectedProperties, bWasRecreated });
	
	FindOrRecreateSavedComponents(*this, WorldData, OriginalActor);
	DeserializeIntoEditorWorldActor(SnapshotWorld, OriginalActor, WorldData, InLocalisationSnapshotPackage, DeserializeActor, DeserializeComponent);
}

void FActorSnapshotData::DeserializeIntoEditorWorldActor(UWorld* SnapshotWorld, AActor* OriginalActor, FWorldSnapshotData& WorldData, UPackage* InLocalisationSnapshotPackage, FSerializeActor SerializeActor, FSerializeComponent SerializeComponent)
{
	const TOptional<AActor*> Deserialized = GetDeserialized(SnapshotWorld, WorldData, OriginalActor, InLocalisationSnapshotPackage);
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
			FSubobjectSnapshotData& SerializedCompData,
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
	    }
	);

	// Fixes up restored attachments
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
		const EComponentCreationMethod CreationMethod = CompIt->Value.CreationMethod;
		
		if (CreationMethod == EComponentCreationMethod::UserConstructionScript)	// Construction script components are not supported 
		{
			continue;
		}

		const int32 SubobjectIndex = CompIt->Key;
		const FSoftObjectPath& OriginalComponentPath = WorldData.SerializedObjectReferences[SubobjectIndex];
		FSubobjectSnapshotData& SnapshotData = WorldData.Subobjects[SubobjectIndex];
		
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

inline void FActorSnapshotData::DeserializeSubobjectsForSnapshotActor(AActor* IntoActor, FWorldSnapshotData& WorldData, UPackage* InLocalisationSnapshotPackage)
{
	for (const int32 SubobjectIndex : OwnedSubobjects)
	{
		check(!ComponentData.Contains(SubobjectIndex));

#if USE_STABLE_LOCALIZATION_KEYS
		const FString LocalisationNamespace = TextNamespaceUtil::EnsurePackageNamespace(InLocalisationSnapshotPackage);
#else
		const FString LocalisationNamespace;
#endif

		// Ensures the object is allocated and serialized into. Return value not needed.
		SnapshotUtil::Object::ResolveObjectDependencyForSnapshotWorld(
			WorldData,
			SubobjectIndex,
			LocalisationNamespace
		);
	}
}
