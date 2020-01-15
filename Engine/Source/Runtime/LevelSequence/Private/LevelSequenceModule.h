// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ILevelSequenceModule.h"

DECLARE_LOG_CATEGORY_EXTERN(LogLevelSequence, Log, All);

/**
 * Implements the LevelSequence module.
 */
class FLevelSequenceModule : public ILevelSequenceModule
{
public:

	// IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	// ILevelSequenceModule interface
	virtual FDelegateHandle RegisterObjectSpawner(FOnCreateMovieSceneObjectSpawner InOnCreateMovieSceneObjectSpawner) override;
	virtual void UnregisterObjectSpawner(FDelegateHandle InHandle) override;

	/** Populate the specified array with all currently registered object spawners */
	void GenerateObjectSpawners(TArray<TSharedRef<IMovieSceneObjectSpawner>>& OutSpawners) const;

public:
	/** List of object spawner delegates used to extend the spawn register */
	TArray< FOnCreateMovieSceneObjectSpawner > OnCreateMovieSceneObjectSpawnerDelegates;

	/** Internal delegate handle used for spawning actors */
	FDelegateHandle OnCreateMovieSceneObjectSpawnerDelegateHandle;
};
