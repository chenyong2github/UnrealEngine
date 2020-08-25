// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "LevelInstance/ILevelInstanceEditorModule.h"

class AActor;
class ULevel;
enum class EMapChangeType : uint8;

/**
 * The module holding all of the UI related pieces for LevelInstance management
 */
class FLevelInstanceEditorModule : public ILevelInstanceEditorModule
{
public:
	virtual ~FLevelInstanceEditorModule(){}

	/**
	 * Called right after the module DLL has been loaded and the module object has been created
	 */
	virtual void StartupModule();

	/**
	 * Called before the module is unloaded, right before the module object is destroyed.
	 */
	virtual void ShutdownModule();

private:
	void OnLevelActorDeleted(AActor* Actor);
	void OnMapChanged(UWorld* World, EMapChangeType MapChangeType);
	void CanMoveActorToLevel(const AActor* ActorToMove, const ULevel* DestLevel, bool& bOutCanMove);

	void ExtendContextMenu();
};
