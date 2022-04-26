// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVR/DMXMVRSceneActor.h"

#include "DMXRuntimeLog.h"
#include "Game/DMXComponent.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXLibrary.h"
#include "MVR/DMXMVRFixtureActorLibrary.h"
#include "MVR/DMXMVRAssetUserData.h"
#include "MVR/DMXMVRFixtureActorInterface.h"

#include "DatasmithAssetUserData.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"

#if WITH_EDITOR
#include "Editor.h"
#endif 


ADMXMVRSceneActor::ADMXMVRSceneActor()
{
#if WITH_EDITOR
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}

	FEditorDelegates::MapChange.AddUObject(this, &ADMXMVRSceneActor::OnMapChange);

	if (GEngine)
	{
		GEngine->OnLevelActorDeleted().AddUObject(this, &ADMXMVRSceneActor::OnActorDeleted);
	}

	if (GEditor)
	{
		GEditor->GetEditorSubsystem<UImportSubsystem>()->OnAssetPostImport.AddUObject(this, &ADMXMVRSceneActor::OnAssetPostImport);
	}
#endif // WITH_EDITOR

	MVRSceneRoot = CreateDefaultSubobject<USceneComponent>("MVRSceneRoot");
	SetRootComponent(MVRSceneRoot);
}

ADMXMVRSceneActor::~ADMXMVRSceneActor()
{
#if WITH_EDITOR
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}

	FEditorDelegates::MapChange.RemoveAll(this);

	if (GEngine)
	{
		GEngine->OnLevelActorDeleted().RemoveAll(this);
	}

	if (GEditor)
	{
		GEditor->GetEditorSubsystem<UImportSubsystem>()->OnAssetPostImport.RemoveAll(this);
	}
#endif // WITH_EDITOR
}

void ADMXMVRSceneActor::PostLoad()
{
	Super::PostLoad();
	SetMVRUUIDsForRelatedActors();
}

void ADMXMVRSceneActor::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

	// If the actor was created as a Datasmith Element, set the library from there
	const FString DMXLibraryPathString = UDatasmithAssetUserData::GetDatasmithUserDataValueForKey(this, TEXT("DMXLibraryPath"));
	if (!DMXLibraryPathString.IsEmpty() && !DMXLibrary)
	{
		const FSoftObjectPath DMXLibraryPath(DMXLibraryPathString);
		UObject* NewDMXLibraryObject = DMXLibraryPath.TryLoad();
		if (UDMXLibrary* NewDMXLibrary = Cast<UDMXLibrary>(NewDMXLibraryObject))
		{
			SetDMXLibrary(NewDMXLibrary);
		}
	}

	SetMVRUUIDsForRelatedActors();
}

void ADMXMVRSceneActor::SetMVRUUIDsForRelatedActors()
{
	for (const TTuple<FGuid, TSoftObjectPtr<AActor>>& MVRUUIDToActorPair : MVRUUIDToRelatedActorMap)
	{
		if (AActor* Actor = MVRUUIDToActorPair.Value.Get())
		{			
			const FString MVRUUID = UDMXMVRAssetUserData::GetMVRAssetUserDataValueForkey(Actor, UDMXMVRAssetUserData::MVRUUIDMetaDataKey);
			if (MVRUUID.IsEmpty())
			{
				if (!UDMXMVRAssetUserData::SetMVRAssetUserDataValueForKey(Actor, UDMXMVRAssetUserData::MVRUUIDMetaDataKey, MVRUUIDToActorPair.Key.ToString()))
				{
					UE_LOG(LogDMXRuntime, Warning, TEXT("Actor %s is referenced by MVR scene but doesn't have an MVR UUID."), *Actor->GetName());
				}
			}
		}
	}
}

void ADMXMVRSceneActor::SetDMXLibrary(UDMXLibrary* NewDMXLibrary)
{
	if (!ensureAlwaysMsgf(!DMXLibrary, TEXT("Tried to set the DMXLibrary for %s, but it already has one set. Changing the library is not supported."), *GetName()))
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (!NewDMXLibrary)
	{
		return;
	}

	if (NewDMXLibrary == DMXLibrary)
	{
		return;
	}

	DMXLibrary = NewDMXLibrary;

	const TSharedRef<FDMXMVRFixtureActorLibrary> MVRFixtureActorLibrary = MakeShared<FDMXMVRFixtureActorLibrary>();
	const TArray<UDMXEntityFixturePatch*> FixturePatches = DMXLibrary->GetEntitiesTypeCast<UDMXEntityFixturePatch>();
	for (UDMXEntityFixturePatch* FixturePatch : FixturePatches)
	{
		UClass* ActorClass = MVRFixtureActorLibrary->FindMostAppropriateActorClassForPatch(FixturePatch);
		if (!ActorClass)
		{
			continue;
		}

		const TArray<UDMXMVRFixtureInstance*>& MVRFixturesInstances = FixturePatch->GetMVRFixtureInstances();
		for (const UDMXMVRFixtureInstance* MVRFixtureInstance : MVRFixturesInstances)
		{
			const FDMXMVRFixture& MVRFixture = MVRFixtureInstance->GetMVRFixture();
			if (!ensureAlwaysMsgf(MVRFixture.UUID.IsValid(), TEXT("Tried to set MVR Fixture %s for MVR Scene Actor %s, but the MVR UUID is invalid."), *MVRFixture.Name, *GetName()))
			{
				continue;
			}

			const FTransform Transform = MVRFixture.Transform.IsSet() ? MVRFixture.Transform.GetValue() : FTransform::Identity;
			AActor* ChildActor = World->SpawnActor<AActor>(ActorClass, Transform);
			if (!ChildActor)
			{
				continue;
			}

			ChildActor->RegisterAllComponents();
			USceneComponent* RootComponentOfChildActor = ChildActor->GetRootComponent();
			if (!RootComponentOfChildActor)
			{
				continue;
			}
			RootComponentOfChildActor->AttachToComponent(MVRSceneRoot, FAttachmentTransformRules::KeepRelativeTransform);
			
			// Set the patch either via the interface or via a present DMX Component.
			// Prefer the interface way as it may further customize how the patch is set.
			if (IDMXMVRFixtureActorInterface* MVRFixtureActorInterface = Cast<IDMXMVRFixtureActorInterface>(ChildActor))
			{
				MVRFixtureActorInterface->Execute_OnMVRSetFixturePatch(ChildActor, FixturePatch);
			}
			else if(UActorComponent* Component = ChildActor->GetComponentByClass(UDMXComponent::StaticClass()))
			{
				CastChecked<UDMXComponent>(Component)->SetFixturePatch(FixturePatch);
			}

			MVRUUIDToRelatedActorMap.Add(MVRFixture.UUID, ChildActor);
		}
	}
}

#if WITH_EDITOR
void ADMXMVRSceneActor::OnMapChange(uint32 MapEventFlags)
{
	// Whenever a sub-level is loaded, we need to apply the fix
	if (MapEventFlags == MapChangeEventFlags::NewMap)
	{
		SetMVRUUIDsForRelatedActors();
	}
}
#endif // WITH_EDITOR

#if WITH_EDITOR
void ADMXMVRSceneActor::OnActorDeleted(AActor* ActorDeleted)
{
	const FString MVRUUIDString = UDMXMVRAssetUserData::GetMVRAssetUserDataValueForkey(ActorDeleted, UDMXMVRAssetUserData::MVRUUIDMetaDataKey);
	FGuid MVRUUID;
	if (FGuid::Parse(MVRUUIDString, MVRUUID))
	{
		TSoftObjectPtr<AActor>* RelatedActorPtr = MVRUUIDToRelatedActorMap.Find(MVRUUID);
		if (RelatedActorPtr)
		{
			AActor* RelatedActor = RelatedActorPtr->Get();
			if (RelatedActor == ActorDeleted)
			{
				// This will add this actor to the transaction if there is one currently recording
				Modify();

				RelatedActorPtr->Reset();
			}
		}
	}
}
#endif // WITH_EDITOR

#if WITH_EDITOR
void ADMXMVRSceneActor::OnAssetPostImport(UFactory* InFactory, UObject* ActorAdded)
{
	for (TObjectIterator<AActor> It; It; ++It)
	{
		AActor* Actor = *It;

		const FString MVRUUIDString = UDMXMVRAssetUserData::GetMVRAssetUserDataValueForkey(Actor, UDMXMVRAssetUserData::MVRUUIDMetaDataKey);
		FGuid MVRUUID;
		if (FGuid::Parse(MVRUUIDString, MVRUUID))
		{
			TSoftObjectPtr< AActor >* RelatedActorPtr = MVRUUIDToRelatedActorMap.Find(MVRUUID);
			if (RelatedActorPtr)
			{
				AActor* RelatedActor = RelatedActorPtr->Get();
				if (!RelatedActor)
				{
					// This will add this actor to the transaction if there is one currently recording
					Modify();

					*RelatedActorPtr = Actor;
				}
			}
		}
	}
}
#endif // WITH_EDITOR
