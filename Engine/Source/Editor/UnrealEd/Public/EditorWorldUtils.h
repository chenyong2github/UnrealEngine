// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	EditorWorldUtils.h: Editor-specific world management utilities
=============================================================================*/

#pragma once

#include "Engine/World.h"

/**
 * A helper RAII class to initialize / destroy an editor world.
 * The world will be added to the root set and initialized as an editor world using the
 * provided initialization values. 
 * This class will also set GWorld & the EditorWorldContext to this world.
 */
class UNREALED_API FScopedEditorWorld
{
public:
	/**
	 * Constructor - Initialize the provided world as an editor world.
	 * @param InWorld					The world to manage.
	 * @param InInitializationValues	The initialization values to use for the world.
	 */
	FScopedEditorWorld(UWorld* InWorld, const UWorld::InitializationValues& InInitializationValues);

	/**
	 * Destructor - Destroy the provided world.
	 */
	~FScopedEditorWorld();

private:
	UWorld* World;
	UWorld* PrevGWorld;
	bool bWorldWasRooted;
	bool bWorldWasInitialized;
};
