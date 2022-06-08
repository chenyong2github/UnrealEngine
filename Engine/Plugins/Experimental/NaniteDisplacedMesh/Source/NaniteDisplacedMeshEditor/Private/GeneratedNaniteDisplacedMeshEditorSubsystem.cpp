// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeneratedNaniteDisplacedMeshEditorSubsystem.h"

#include "Containers/ChunkedArray.h"
#include "Engine/Engine.h"
#include "Templates/SubclassOf.h"
#include "UObject/UObjectGlobals.h"

void UGeneratedNaniteDisplacedMeshEditorSubsystem::RegisterClassHandler(const TSubclassOf<AActor>& ActorClass, FActorClassHandler&& ActorClassHandler)
{
	ActorClassHandlers.Add(ActorClass.Get(), MoveTemp(ActorClassHandler));
}

void UGeneratedNaniteDisplacedMeshEditorSubsystem::UnregisterClassHandler(const TSubclassOf<AActor>& ActorClass)
{
	const UClass* ClassToRemove = ActorClass.Get();
	ActorClassHandlers.Remove(ClassToRemove);

	TSet<UClass*> SubClassesRegistered;
	for (const TPair<UClass*, FActorClassHandler>& ActorHandler : ActorClassHandlers)
	{
		if (ActorHandler.Key->IsChildOf(ClassToRemove))
		{
			SubClassesRegistered.Add(ActorHandler.Key);
		}
	}

	TChunkedArray<TObjectKey<AActor>> ActorsToRemove;
	for (const TPair<TObjectKey<AActor>, TArray<TObjectKey<UObject>>>& ActorToDependencies : ActorsToDependencies)
	{
		const AActor* Actor = ActorToDependencies.Key.ResolveObjectPtr();
		if (!Actor)
		{
			// Clean invalid the actors
			ActorsToRemove.AddElement(ActorToDependencies.Key);
			continue;
		}

		UClass* Class = Actor->GetClass();
		while (Class)
		{
			if (SubClassesRegistered.Find(Class))
			{
				break;
			}

			if (Class == ClassToRemove)
			{
				ActorsToRemove.AddElement(ActorToDependencies.Key);
				break;
			}

			Class = Class->GetSuperClass();
		}
	}


	for (const TObjectKey<AActor>& Actor : ActorsToRemove)
	{
		RemoveActor(Actor, GetTypeHash(Actor));
	}
}

void UGeneratedNaniteDisplacedMeshEditorSubsystem::UpdateActorDependencies(AActor* Actor, TArray<TObjectKey<UObject>>&& Dependencies)
{
	if (!FindClassHandler(Actor->GetClass()))
	{
		ensure(false);
		return;
	}

	Dependencies.RemoveAll([this](const TObjectKey<UObject>& WeakObject)
		{
			const bool bCanObjectBeTracked = CanObjectBeTracked(WeakObject.ResolveObjectPtr());
			ensure(bCanObjectBeTracked);
			return !bCanObjectBeTracked;
		});


	if (Dependencies.IsEmpty())
	{
		RemoveActor(Actor);
		return;
	}

	TObjectKey<AActor> WeakActor(Actor);
	uint32 WeakActorHash = GetTypeHash(WeakActor);
	TArray<TObjectKey<UObject>> RegistredActorDependencies = ActorsToDependencies.FindOrAddByHash(WeakActorHash, WeakActor);
	RegistredActorDependencies = MoveTemp(Dependencies);

	for (const TObjectKey<UObject>& Dependency : RegistredActorDependencies)
	{
		DependenciesToActors.FindOrAdd(Dependency).AddByHash(WeakActorHash, WeakActor);
	}
}

void UGeneratedNaniteDisplacedMeshEditorSubsystem::RemoveActor(AActor* ActorToRemove)
{
	TObjectKey<AActor> WeakActorToRemove(ActorToRemove);
	RemoveActor(WeakActorToRemove, GetTypeHash(WeakActorToRemove));
}

void UGeneratedNaniteDisplacedMeshEditorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (GEngine)
	{
		OnObjectsReplacedHandle = FCoreUObjectDelegates::OnObjectsReplaced.AddUObject(this, &UGeneratedNaniteDisplacedMeshEditorSubsystem::OnObjectsReplaced);
		OnLevelActorDeletedHandle = GEngine->OnLevelActorDeleted().AddUObject(this, &UGeneratedNaniteDisplacedMeshEditorSubsystem::OnLevelActorDeleted);
		OnPostEditChangeHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject(this, &UGeneratedNaniteDisplacedMeshEditorSubsystem::OnObjectPostEditChange);
	}
}

void UGeneratedNaniteDisplacedMeshEditorSubsystem::Deinitialize()
{
	Super::Deinitialize();

	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(OnPostEditChangeHandle);
	if (GEngine)
	{
		GEngine->OnLevelActorDeleted().Remove(OnLevelActorDeletedHandle);
	}
	FCoreUObjectDelegates::OnObjectsReplaced.Remove(OnObjectsReplacedHandle);

	ActorClassHandlers.Empty();
	ActorsToDependencies.Empty();
	DependenciesToActors.Empty();
}

void UGeneratedNaniteDisplacedMeshEditorSubsystem::OnObjectPostEditChange(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive && CanObjectBeTracked(Object))
	{
		TObjectKey<UObject> WeakObject(Object);
		uint32 WeakObjectHash = GetTypeHash(WeakObject);
		if (TSet<TObjectKey<AActor>>* PointerToDependentActors = DependenciesToActors.FindByHash(WeakObjectHash, WeakObject))
		{
			bool bActorRemoved = false;

			// Need to copy the data from the set since the handler can modify this set
			TArray<TObjectKey<AActor>> DependentActors = PointerToDependentActors->Array();

			for (const TObjectKey<AActor>& DependentActor : DependentActors)
			{
				if (AActor* RawActor = DependentActor.ResolveObjectPtr())
				{
					if (FActorClassHandler* ClassHandler = FindClassHandler(RawActor->GetClass()))
					{
						if (ShouldCallback(Object->GetClass(), *ClassHandler, PropertyChangedEvent))
						{
							ClassHandler->Callback(RawActor, Object, PropertyChangedEvent);
						}
					}
				}
				else
				{
					bActorRemoved |= RemoveActor(DependentActor, GetTypeHash(DependentActor));
				}
			}
		}
	}
}

void UGeneratedNaniteDisplacedMeshEditorSubsystem::OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementMap)
{
	for (const TPair<UObject*, UObject*>& ReplacedObject : ReplacementMap)
	{
		UObject* OldObject = ReplacedObject.Key;
		UObject* NewObject = ReplacedObject.Value;

		if (UClass* OldClass = Cast<UClass>(OldObject))
		{
			UClass* NewClass = Cast<UClass>(NewObject);
			uint32 NewClassHash = GetTypeHash(NewClass);

			// Patch the class handler
			FActorClassHandler RemovedClassHandler;
			uint32 OldClassHash = GetTypeHash(OldClass);
			if (ActorClassHandlers.RemoveAndCopyValueByHash(OldClassHash, OldClass, RemovedClassHandler))
			{
				if (NewClass)
				{
					ActorClassHandlers.AddByHash(NewClassHash, NewClass, MoveTemp(RemovedClassHandler));
				}
			}
			
			// Patch the properties to watch
			for (TPair<UClass*, FActorClassHandler>& ActorClassHandler : ActorClassHandlers)
			{
				TSet<FProperty*> OldPropertiesToWatch;
				if (ActorClassHandler.Value.PropertiesToWatchPerAssetType.RemoveAndCopyValueByHash(OldClassHash, OldClass, OldPropertiesToWatch))
				{
					if (NewClass)
					{
						TSet<FProperty*> NewPropertyToWatch;
						NewPropertyToWatch.Reserve(OldPropertiesToWatch.Num());

						for (FProperty* OdlProperty : OldPropertiesToWatch)
						{
							if (FProperty* NewProperty = NewClass->FindPropertyByName(OdlProperty->GetFName()))
							{
								NewPropertyToWatch.Add(NewProperty);
							}
						}

						if (!NewPropertyToWatch.IsEmpty())
						{
							ActorClassHandler.Value.PropertiesToWatchPerAssetType.AddByHash(NewClassHash, NewClass, MoveTemp(NewPropertyToWatch));
						}
					}
				}
			}
		}
		else if (AActor* OldActor = Cast<AActor>(OldObject))
		{
			// Path the actor notification
			TArray<TObjectKey<UObject>> AssetDependencies;
			TObjectKey<AActor> WeakOldActor(OldActor);
			uint32 WeakOldActorHash = GetTypeHash(WeakOldActor);
			if (ActorsToDependencies.RemoveAndCopyValueByHash(WeakOldActorHash, WeakOldActor, AssetDependencies))
			{
				AActor* NewActor = Cast<AActor>(NewObject);
				const bool bIsNewActorValid = IsValid(NewActor);
				TObjectKey<AActor> WeakNewActor(NewActor);
				uint32 WeakNewActorHash = GetTypeHash(WeakNewActor);

				for (const TObjectKey<UObject>& AssetDependency : AssetDependencies)
				{
					uint32 AssetDependencyHash = GetTypeHash(AssetDependency);
					if (TSet<TObjectKey<AActor>>* PointerToActors = DependenciesToActors.FindByHash(AssetDependencyHash, AssetDependency))
					{
						PointerToActors->RemoveByHash(WeakOldActorHash, WeakOldActor);

						if (bIsNewActorValid)
						{
							PointerToActors->AddByHash(WeakNewActorHash, WeakNewActor);
						}
						else if (PointerToActors->IsEmpty())
						{
							DependenciesToActors.RemoveByHash(AssetDependencyHash, AssetDependency);
						}
					}
				}

				if (bIsNewActorValid)
				{
					ActorsToDependencies.AddByHash(WeakNewActorHash, WeakNewActor, MoveTemp(AssetDependencies));
				}
			}
		}
		else
		{
			// Patch the asset change tracking
			TSet<TObjectKey<AActor>> DependentActors;
			if (DependenciesToActors.RemoveAndCopyValue(OldObject, DependentActors))
			{
				const bool bIsNewObjectValid = CanObjectBeTracked(NewObject);
				TObjectKey<UObject> WeakNewObject(NewObject);

				for (const TObjectKey<AActor>& DependentActor : DependentActors)
				{
					uint32 DependentActorHash = GetTypeHash(DependentActor)
;					if (TArray<TObjectKey<UObject>>* PointerToDependencies = ActorsToDependencies.FindByHash(DependentActorHash, DependentActor))
					{
						PointerToDependencies->Remove(OldObject);

						if (bIsNewObjectValid)
						{
							PointerToDependencies->Add(WeakNewObject);
						}
						else if (PointerToDependencies->IsEmpty())
						{
							ActorsToDependencies.RemoveByHash(DependentActorHash, DependentActor);
						}
					}
				}

				if (bIsNewObjectValid)
				{
					DependenciesToActors.Add(WeakNewObject, MoveTemp(DependentActors));
				}
				
			}
		}
	}
}

void UGeneratedNaniteDisplacedMeshEditorSubsystem::OnLevelActorDeleted(AActor* Actor)
{
	TObjectKey<AActor> WeakActor(Actor);
	uint32 WeakActorHash = GetTypeHash(WeakActor);
	RemoveActor(WeakActor, WeakActorHash);
}

bool UGeneratedNaniteDisplacedMeshEditorSubsystem::CanObjectBeTracked(UObject* InObject)
{
	// Only assets can be tracked otherwise we might not receive the callbacks we need for this system to be functional and safe
	return InObject && InObject->IsAsset();
}

bool UGeneratedNaniteDisplacedMeshEditorSubsystem::RemoveActor(const TObjectKey<AActor>& InActorToRemove, uint32 InWeakActorHash)
{
	TArray<TObjectKey<UObject>> Dependencies;
	if (ActorsToDependencies.RemoveAndCopyValueByHash(InWeakActorHash, InActorToRemove, Dependencies))
	{
		for (const TObjectKey<UObject>& Asset : Dependencies)
		{
			uint32 AssetHash = GetTypeHash(Asset);
			if (TSet<TObjectKey<AActor>>* PointerToActorSet = DependenciesToActors.FindByHash(AssetHash, Asset))
			{
				PointerToActorSet->RemoveByHash(InWeakActorHash, InActorToRemove);
				if (PointerToActorSet->IsEmpty())
				{
					DependenciesToActors.RemoveByHash(AssetHash, Asset);
				}
			}
		}

		return true;
	}

	return false;
}

UGeneratedNaniteDisplacedMeshEditorSubsystem::FActorClassHandler* UGeneratedNaniteDisplacedMeshEditorSubsystem::FindClassHandler(UClass* Class)
{
	while (Class)
	{
		if (FActorClassHandler* ClassHandler = ActorClassHandlers.Find(Class))
		{
			return ClassHandler;
		}

		Class = Class->GetSuperClass();
	}

	return nullptr;
}

bool UGeneratedNaniteDisplacedMeshEditorSubsystem::ShouldCallback(UClass* AssetClass, const FActorClassHandler& ClassHandler, const FPropertyChangedEvent& PropertyChangedEvent)
{
	if (!PropertyChangedEvent.Property)
	{
		return true;
	}

	while (AssetClass)
	{
		if (const TSet<FProperty*>* PropertiesToWatch = ClassHandler.PropertiesToWatchPerAssetType.Find(AssetClass))
		{
			return bool(PropertiesToWatch->Find(PropertyChangedEvent.Property));
		}

		AssetClass = AssetClass->GetSuperClass();
	}

	// Default to true if we don't have any info on the type
	return true;
}
