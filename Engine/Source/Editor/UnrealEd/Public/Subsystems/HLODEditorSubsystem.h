// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorSubsystem.h"
#include "Engine/World.h"
#include "HLODEditorSubsystem.generated.h"

UCLASS()
class UNREALED_API UHLODEditorSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem Interface.
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem Interface.

	// Should be called when the "Save LOD Actors to HLOD Packages" option is toggled.
	void OnSaveLODActorsToHLODPackagesChanged();

private:
	// Recreate LOD actors for all levels in the provided world.
	void RecreateLODActorsForWorld(UWorld* InWorld, const UWorld::InitializationValues InInitializationValues);
	
	// Recreate LOD actors for the given level.
	void RecreateLODActorsForLevel(ULevel* InLevel, UWorld* InWorld);

	void OnPreSaveWorld(uint32 InSaveFlags, UWorld* InWorld);

	void UnregisterRecreateLODActorsDelegates();
	void RegisterRecreateLODActorsDelegates();

private:
	FDelegateHandle OnPostWorldInitializationDelegateHandle;
	FDelegateHandle OnLevelAddedToWorldDelegateHandle;
	FDelegateHandle OnPreSaveWorlDelegateHandle;
};
