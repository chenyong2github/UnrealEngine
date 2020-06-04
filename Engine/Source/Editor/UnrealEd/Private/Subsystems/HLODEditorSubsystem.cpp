// Copyright Epic Games, Inc. All Rights Reserved.

#include "Subsystems/HLODEditorSubsystem.h"
#include "Engine/EngineTypes.h"
#include "Engine/LODActor.h"
#include "Engine/HLODProxy.h"
#include "Editor.h"
#include "HierarchicalLOD.h"
#include "Modules/ModuleManager.h"
#include "IHierarchicalLODUtilities.h"
#include "HierarchicalLODUtilitiesModule.h"
#include "GameFramework/WorldSettings.h"


void UHLODEditorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	RegisterRecreateLODActorsDelegates();
}

void UHLODEditorSubsystem::Deinitialize()
{
	UnregisterRecreateLODActorsDelegates();
	Super::Deinitialize();
}

void UHLODEditorSubsystem::OnSaveLODActorsToHLODPackagesChanged()
{
	UnregisterRecreateLODActorsDelegates();
	RegisterRecreateLODActorsDelegates();
}

void UHLODEditorSubsystem::UnregisterRecreateLODActorsDelegates()
{
	FWorldDelegates::OnPostWorldInitialization.Remove(OnPostWorldInitializationDelegateHandle);
	FWorldDelegates::LevelAddedToWorld.Remove(OnLevelAddedToWorldDelegateHandle);
}

void UHLODEditorSubsystem::RegisterRecreateLODActorsDelegates()
{
	if (GetDefault<UHierarchicalLODSettings>()->bSaveLODActorsToHLODPackages)
	{
		OnPostWorldInitializationDelegateHandle = FWorldDelegates::OnPostWorldInitialization.AddUObject(this, &UHLODEditorSubsystem::RecreateLODActorsForWorld);
		OnLevelAddedToWorldDelegateHandle = FWorldDelegates::LevelAddedToWorld.AddUObject(this, &UHLODEditorSubsystem::RecreateLODActorsForLevel);
	}	
}

void UHLODEditorSubsystem::RecreateLODActorsForWorld(UWorld* InWorld, const UWorld::InitializationValues InInitializationValues)
{
	// For each level in this world
	for (ULevel* Level : InWorld->GetLevels())
	{
		RecreateLODActorsForLevel(Level, InWorld);
	}
}

void UHLODEditorSubsystem::RecreateLODActorsForLevel(ULevel* InLevel, UWorld* InWorld)
{
	bool bShouldRecreateActors = (InLevel->GetWorld()->WorldType == EWorldType::Editor) || (InLevel->GetWorldSettings()->GetLocalRole() == ROLE_Authority);
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
