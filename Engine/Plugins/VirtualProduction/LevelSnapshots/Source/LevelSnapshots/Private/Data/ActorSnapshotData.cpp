// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/ActorSnapshotData.h"

#include "Archive/ApplySnapshotToEditorArchive.h"
#include "Archive/TakeWorldObjectSnapshotArchive.h"
#include "Data/WorldSnapshotData.h"
#include "Data/SnapshotCustomVersion.h"
#include "CustomSerialization/CustomObjectSerializationWrapper.h"
#include "Data/Util/ActorHashUtil.h"
#include "Data/Util/SnapshotObjectUtil.h"
#include "LevelSnapshotsLog.h"
#include "LevelSnapshotsModule.h"
#include "LoadSnapshotObjectArchive.h"
#include "PropertySelectionMap.h"
#include "RestorationEvents/ApplySnapshotToActorScope.h"
#include "SnapshotConsoleVariables.h"
#include "SnapshotRestorability.h"
#include "Util/EquivalenceUtil.h"
#include "Util/Component/SnapshotComponentUtil.h"

#include "Components/ActorComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Templates/NonNullPointer.h"
#include "UObject/Package.h"
#include "UObject/Script.h"

#if USE_STABLE_LOCALIZATION_KEYS
#include "Internationalization/TextPackageNamespaceUtil.h"
#endif

namespace
{
	void ConditionBreakOnActor(AActor* Actor)
	{
		const FString NameToSearchFor = SnapshotCVars::CVarBreakOnSnapshotActor.GetValueOnAnyThread();
		if (!NameToSearchFor.IsEmpty() && Actor->GetName().Contains(NameToSearchFor))
		{
			UE_DEBUG_BREAK();
		}
	}
}


FActorSnapshotData FActorSnapshotData::SnapshotActor(AActor* OriginalActor, FWorldSnapshotData& WorldData)
{
	ConditionBreakOnActor(OriginalActor);
	
	FActorSnapshotData Result;
	UClass* ActorClass = OriginalActor->GetClass();
	Result.ActorClass = ActorClass;
	
	FTakeWorldObjectSnapshotArchive::TakeSnapshot(Result.SerializedActorData, WorldData, OriginalActor);
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

	SnapshotUtil::PopulateActorHash(Result.Hash, OriginalActor);
#if WITH_EDITORONLY_DATA
	Result.ActorLabel = OriginalActor->GetActorLabel();
#endif
	return Result;
}

void FActorSnapshotData::ResetTransientData()
{
	CachedSnapshotActor.Reset();
	bReceivedSerialisation = false;
#if WITH_EDITORONLY_DATA
	bHasBeenDiffed = false;
#endif
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

		SnapshotUtil::Component::AllocateMissingComponentsForSnapshotActor(CachedSnapshotActor.Get(), *this, WorldData);
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
	UE_LOG(LogLevelSnapshots, Verbose, TEXT("========== Get Deserialized %s =========="), *OriginalActorPath.ToString());

	const TOptional<AActor*> Preallocated = GetPreallocated(SnapshotWorld, WorldData);
	if (!Preallocated)
	{
		return {};
	}
	bReceivedSerialisation = true;

	const auto ProcessObjectDependency = [this](int32 OriginalObjectDependency)
	{
		ObjectDependencies.Add(OriginalObjectDependency);
	};
	AActor* PreallocatedActor = Preallocated.GetValue();
	{
		const FRestoreObjectScope FinishRestore = FCustomObjectSerializationWrapper::PreActorRestore_SnapshotWorld(PreallocatedActor, CustomActorSerializationData, WorldData, ProcessObjectDependency, InLocalisationSnapshotPackage);
		FLoadSnapshotObjectArchive::ApplyToSnapshotWorldObject(SerializedActorData, WorldData, PreallocatedActor, ProcessObjectDependency, InLocalisationSnapshotPackage);
#if WITH_EDITOR
		UE_LOG(LogLevelSnapshots, Verbose, TEXT("ActorLabel is \"%s\" for \"%s\" (editor object path \"%s\")"), *PreallocatedActor->GetActorLabel(), *PreallocatedActor->GetPathName(), *OriginalActorPath.ToString());
#endif
	}

	DeserializeComponents(PreallocatedActor, WorldData,
		[&WorldData, &ProcessObjectDependency, InLocalisationSnapshotPackage](
			FSubobjectSnapshotData& SerializedCompData,
			FComponentSnapshotData& CompData,
			UActorComponent* Comp,
			const FSoftObjectPath& OriginalComponentPath,
			FWorldSnapshotData& SharedData)
		{
			SerializedCompData.SnapshotObject = Comp;
			
			const FRestoreObjectScope FinishRestore = FCustomObjectSerializationWrapper::PreSubobjectRestore_SnapshotWorld(Comp, OriginalComponentPath, WorldData, ProcessObjectDependency, InLocalisationSnapshotPackage);
			CompData.DeserializeIntoTransient(SerializedCompData, Comp, SharedData, ProcessObjectDependency, InLocalisationSnapshotPackage);
		}
	);

	DeserializeSubobjectsForSnapshotActor(PreallocatedActor, WorldData, ProcessObjectDependency, InLocalisationSnapshotPackage);
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

namespace ActorSnapshotData
{
	static void PreventAttachParentInfiniteRecursion(UActorComponent* Original, const FPropertySelection& PropertySelection)
	{
		static const FProperty* AttachParent = USceneComponent::StaticClass()->FindPropertyByName(FName("AttachParent"));

		// Suppose snapshot contains Root > Child and now the hierarchy is Child > Root.
		// Root's AttachChildren still contains Child after we apply snapshot since that property is transient.
		// Solution: Detach now, then serialize AttachParent, and OnRegister will automatically call AttachToComponent and update everything.
		USceneComponent* SceneComponent = Cast<USceneComponent>(Original);
		if (Original && PropertySelection.IsPropertySelected(nullptr, AttachParent))
		{
			SceneComponent->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
		}
	}
}

void FActorSnapshotData::DeserializeIntoExistingWorldActor(UWorld* SnapshotWorld, AActor* OriginalActor, FWorldSnapshotData& WorldData, UPackage* InLocalisationSnapshotPackage, const FPropertySelectionMap& SelectedProperties)
{
	UE_LOG(LogLevelSnapshots, Verbose, TEXT("========== Apply existing %s =========="), *OriginalActor->GetPathName());
	const bool bWasRecreated = false;
	const FApplySnapshotToActorScope NotifyExternalListeners({ OriginalActor, SelectedProperties, bWasRecreated });
	
	SnapshotUtil::Component::AddAndRemoveComponentsSelectedForRestore(OriginalActor, *this, WorldData, SelectedProperties, InLocalisationSnapshotPackage);

	auto DeserializeActor = [this, &WorldData, InLocalisationSnapshotPackage, &SelectedProperties](AActor* OriginalActor, AActor* DeserializedActor)
	{
		const FRestoreObjectScope FinishRestore = FCustomObjectSerializationWrapper::PreActorRestore_EditorWorld(OriginalActor, CustomActorSerializationData, WorldData, SelectedProperties, InLocalisationSnapshotPackage);
		const FPropertySelection* ActorPropertySelection = SelectedProperties.GetObjectSelection(OriginalActor).GetPropertySelection();
		if (ActorPropertySelection)
		{
			FApplySnapshotToEditorArchive::ApplyToExistingEditorWorldObject(SerializedActorData, WorldData, OriginalActor, DeserializedActor, SelectedProperties, *ActorPropertySelection);
		}
	};
	auto DeserializeComponent = [InLocalisationSnapshotPackage, &SelectedProperties, &WorldData](FSubobjectSnapshotData& SerializedCompData, FComponentSnapshotData& CompData, UActorComponent* Original, UActorComponent* Deserialized)
	{
		const FRestoreObjectScope FinishRestore = FCustomObjectSerializationWrapper::PreSubobjectRestore_EditorWorld(Deserialized, Original, WorldData, SelectedProperties, InLocalisationSnapshotPackage);
		const FPropertySelection* ComponentSelectedProperties = SelectedProperties.GetObjectSelection(Original).GetPropertySelection();
		if (ComponentSelectedProperties)
		{
			ActorSnapshotData::PreventAttachParentInfiniteRecursion(Original, *ComponentSelectedProperties);
			FApplySnapshotToEditorArchive::ApplyToExistingEditorWorldObject(SerializedCompData, WorldData, Original, Deserialized, SelectedProperties, *ComponentSelectedProperties);
		};
	};
	DeserializeIntoEditorWorldActor(SnapshotWorld, OriginalActor, WorldData, InLocalisationSnapshotPackage, DeserializeActor, DeserializeComponent);
}

void FActorSnapshotData::DeserializeIntoRecreatedEditorWorldActor(UWorld* SnapshotWorld, AActor* OriginalActor, FWorldSnapshotData& WorldData, UPackage* InLocalisationSnapshotPackage, const FPropertySelectionMap& SelectedProperties)
{
	UE_LOG(LogLevelSnapshots, Verbose, TEXT("========== Apply recreated %s =========="), *OriginalActor->GetPathName());
	const bool bWasRecreated = true;
	const FApplySnapshotToActorScope NotifyExternalListeners({ OriginalActor, SelectedProperties, bWasRecreated });
	
	SnapshotUtil::Component::AllocateMissingComponentsForRecreatedActor(OriginalActor, *this, WorldData);

	auto DeserializeActor = [this, &WorldData, InLocalisationSnapshotPackage, &SelectedProperties](AActor* OriginalActor, AActor* DeserializedActor)
	{
		const FRestoreObjectScope FinishRestore = FCustomObjectSerializationWrapper::PreActorRestore_EditorWorld(OriginalActor, CustomActorSerializationData, WorldData, SelectedProperties, InLocalisationSnapshotPackage);
		FApplySnapshotToEditorArchive::ApplyToRecreatedEditorWorldObject(SerializedActorData, WorldData, OriginalActor, DeserializedActor, SelectedProperties);
	};
	auto DeserializeComponent = [InLocalisationSnapshotPackage, &WorldData, &SelectedProperties](FSubobjectSnapshotData& SerializedCompData, FComponentSnapshotData& CompData, UActorComponent* Original, UActorComponent* Deserialized)
	{
		const FRestoreObjectScope FinishRestore = FCustomObjectSerializationWrapper::PreSubobjectRestore_EditorWorld(Deserialized, Original, WorldData, SelectedProperties, InLocalisationSnapshotPackage);
		FApplySnapshotToEditorArchive::ApplyToRecreatedEditorWorldObject(SerializedCompData, WorldData, Original, Deserialized, SelectedProperties); 
	};
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

	const AActor* AttachParentBeforeRestore = OriginalActor->GetAttachParentActor();
	SerializeActor(OriginalActor, *Deserialized);
#if WITH_EDITOR
	UE_LOG(LogLevelSnapshots, Verbose, TEXT("ActorLabel is \"%s\" for \"%s\""), *OriginalActor->GetActorLabel(), *OriginalActor->GetPathName());
#endif

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
			
			UE_CLOG(!DeserializedCompCounterpart, LogLevelSnapshots, Warning, TEXT("Failed to find component called %s on temp deserialized snapshot actor. Skipping component..."), *OriginalCompName.ToString())
	        if (DeserializedCompCounterpart)
	        {
	        	SerializeComponent(SerializedCompData, CompData, Comp, *DeserializedCompCounterpart);
			
				// We may have modified render information, e.g. for lights we may have changed intensity or colour
				// It may be more efficient to track whether we actually changed render state
				Comp->ReregisterComponent();
	        }
	    }
	);

	// Fixes up restored attachments
	OriginalActor->UpdateComponentTransforms();

#if WITH_EDITOR
	// Update World Outliner. Usually called by USceneComponent::AttachToComponent.
	const AActor* AttachParentAfterRestore = OriginalActor->GetAttachParentActor();
	const bool bAttachParentChanged = AttachParentBeforeRestore != AttachParentAfterRestore;
	if (bAttachParentChanged)
	{
		GEngine->BroadcastLevelActorAttached(OriginalActor, AttachParentAfterRestore);
	}
#endif
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
		if (UActorComponent* ComponentToRestore = SnapshotUtil::FindMatchingComponent(IntoActor, OriginalComponentPath))
		{
			FSubobjectSnapshotData& SnapshotData = WorldData.Subobjects[SubobjectIndex];
			Callback(SnapshotData, CompIt->Value, ComponentToRestore, OriginalComponentPath, WorldData);
		}
	}
}

inline void FActorSnapshotData::DeserializeSubobjectsForSnapshotActor(AActor* IntoActor, FWorldSnapshotData& WorldData, const FProcessObjectDependency& ProcessObjectDependency, UPackage* InLocalisationSnapshotPackage)
{
	for (const int32 SubobjectIndex : OwnedSubobjects)
	{
		check(!ComponentData.Contains(SubobjectIndex));

		FString LocalisationNamespace;
#if USE_STABLE_LOCALIZATION_KEYS
		LocalisationNamespace = TextNamespaceUtil::EnsurePackageNamespace(InLocalisationSnapshotPackage);
#endif

		// Ensures the object is allocated and serialized into. Return value not needed.
		SnapshotUtil::Object::ResolveObjectDependencyForSnapshotWorld(
			WorldData,
			SubobjectIndex,
			ProcessObjectDependency,
			LocalisationNamespace
		);
	}
}
