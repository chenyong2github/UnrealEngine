// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorWorldUtils.h"
#include "Editor.h"


FScopedEditorWorld::FScopedEditorWorld(UWorld* InWorld, const UWorld::InitializationValues& InInitializationValues)
	: World(InWorld)
	, bWorldWasRooted(false)
	, bWorldWasInitialized(false)
{
	check(InWorld);

	bWorldWasRooted = World->IsRooted();
	if (!bWorldWasRooted)
	{
		World->AddToRoot();
	}	

	// Setup the world if needed
	bWorldWasInitialized = World->bIsWorldInitialized;
	if (!bWorldWasInitialized)
	{
		World->WorldType = EWorldType::Editor;

		World->InitWorld(InInitializationValues);
		World->PersistentLevel->UpdateModelComponents();
		World->UpdateWorldComponents(true /*bRerunConstructionScripts*/, false /*bCurrentLevelOnly*/);
		World->UpdateLevelStreaming();
	}
	else
	{
		check(World->WorldType == EWorldType::Editor);
	}

	// Restore previous GWorld / WorldContext
	FWorldContext& WorldContext = GEditor->GetEditorWorldContext(true);
	WorldContext.SetCurrentWorld(World);
	PrevGWorld = GWorld;
	GWorld = World;
}

FScopedEditorWorld::~FScopedEditorWorld()
{
	// Destroy world
	if (!bWorldWasInitialized)
	{
		World->DestroyWorld(false /*bBroadcastWorldDestroyedEvent*/);
	}
	else if (!bWorldWasRooted)
	{
		// Unroot world
		World->RemoveFromRoot();
	}

	// Restore previous GWorld / WorldContext
	FWorldContext& WorldContext = GEditor->GetEditorWorldContext(true);
	WorldContext.SetCurrentWorld(PrevGWorld);
	GWorld = PrevGWorld;
}
