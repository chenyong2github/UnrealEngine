// Copyright Epic Games, Inc. All Rights Reserved.

#include "HLOD/HLODEngineSubsystem.h"

#if WITH_EDITOR

#include "EngineUtils.h"
#include "Engine/EngineTypes.h"
#include "Engine/LODActor.h"
#include "Engine/HLODProxy.h"
#include "Editor.h"
#include "HierarchicalLOD.h"
#include "Modules/ModuleManager.h"
#include "IHierarchicalLODUtilities.h"
#include "HierarchicalLODUtilitiesModule.h"
#include "GameFramework/WorldSettings.h"

void UHLODEngineSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	RegisterRecreateLODActorsDelegates();
}

void UHLODEngineSubsystem::Deinitialize()
{
	UnregisterRecreateLODActorsDelegates();
	Super::Deinitialize();
}

void UHLODEngineSubsystem::OnSaveLODActorsToHLODPackagesChanged()
{
	UnregisterRecreateLODActorsDelegates();
	RegisterRecreateLODActorsDelegates();
}

void UHLODEngineSubsystem::UnregisterRecreateLODActorsDelegates()
{
	FWorldDelegates::OnPostWorldInitialization.Remove(OnPostWorldInitializationDelegateHandle);
	FWorldDelegates::LevelAddedToWorld.Remove(OnLevelAddedToWorldDelegateHandle);
}

void UHLODEngineSubsystem::RegisterRecreateLODActorsDelegates()
{
	if (GetDefault<UHierarchicalLODSettings>()->bSaveLODActorsToHLODPackages)
	{
		OnPostWorldInitializationDelegateHandle = FWorldDelegates::OnPostWorldInitialization.AddUObject(this, &UHLODEngineSubsystem::RecreateLODActorsForWorld);
		OnLevelAddedToWorldDelegateHandle = FWorldDelegates::LevelAddedToWorld.AddUObject(this, &UHLODEngineSubsystem::RecreateLODActorsForLevel);
		OnPreSaveWorlDelegateHandle = FEditorDelegates::PreSaveWorld.AddUObject(this, &UHLODEngineSubsystem::OnPreSaveWorld);
	}	
}

void UHLODEngineSubsystem::RecreateLODActorsForWorld(UWorld* InWorld, const UWorld::InitializationValues InInitializationValues)
{
	// For each level in this world
	for (ULevel* Level : InWorld->GetLevels())
	{
		RecreateLODActorsForLevel(Level, InWorld);
	}
}

void UHLODEngineSubsystem::RecreateLODActorsForLevel(ULevel* InLevel, UWorld* InWorld)
{
	bool bShouldRecreateActors = InWorld && !InWorld->bIsTearingDown && ((InWorld->WorldType == EWorldType::Editor) || (InLevel->GetWorldSettings()->GetLocalRole() == ROLE_Authority));
	if (!bShouldRecreateActors)
	{
		return;
	}

	FHierarchicalLODUtilitiesModule& Module = FModuleManager::LoadModuleChecked<FHierarchicalLODUtilitiesModule>("HierarchicalLODUtilities");
	IHierarchicalLODUtilities* Utilities = Module.GetUtilities();

	// First, destroy LODActors that were previously constructed from HLODDesc... If needed, they will be recreated below.
	for (AActor* Actor : InLevel->Actors)
	{
		if (ALODActor* LODActor = Cast<ALODActor>(Actor))
		{
			if (LODActor->WasBuiltFromHLODDesc())
			{
				InLevel->GetWorld()->EditorDestroyActor(LODActor, true);
			}
		}
	}

	// Look for HLODProxy packages associated with this level
	int32 NumLODLevels = InLevel->GetWorldSettings()->GetHierarchicalLODSetup().Num();
	for (int32 LODIndex = 0; LODIndex < NumLODLevels; ++LODIndex)
	{
		// Obtain HLOD package for the current HLOD level
		UHLODProxy* HLODProxy = Utilities->RetrieveLevelHLODProxy(InLevel, LODIndex);
		if (HLODProxy)
		{
			// Spawn LODActors from the HLODDesc, if any is found
			HLODProxy->SpawnLODActors(InLevel);
		}
	}
}

void UHLODEngineSubsystem::OnPreSaveWorld(uint32 InSaveFlags, UWorld* InWorld)
{
	// When cooking, make sure that the LODActors are not transient
	if (InWorld && InWorld->PersistentLevel && GIsCookerLoadingPackage)
	{
		for (TActorIterator<ALODActor> It(InWorld); It; ++It)
		{
			ALODActor* LODActor = *It;
			if (LODActor->WasBuiltFromHLODDesc())
			{
				EObjectFlags TransientFlags = EObjectFlags::RF_Transient | EObjectFlags::RF_DuplicateTransient;
				if (LODActor->HasAnyFlags(TransientFlags))
				{
					LODActor->ClearFlags(TransientFlags);

					const bool bIncludeNestedObjects = true;
					ForEachObjectWithOuter(LODActor, [TransientFlags](UObject* Subobject)
					{
						Subobject->ClearFlags(TransientFlags);
					}, bIncludeNestedObjects);
				}
			}
		}
	}
}

#endif // WITH_EDITOR
