// Copyright Epic Games, Inc. All Rights Reserved.

#include "SpawnHelper.h"

FSpawnHelper::~FSpawnHelper() 
{
	if (GameWorld)
	{
		GameWorld->RemoveFromRoot();
		GameWorld = nullptr;
	}
}

UWorld& FSpawnHelper::GetWorld()
{
	if (!GameWorld)
	{
		GameWorld = CreateWorld();
		check(GameWorld);
		GameWorld->AddToRoot();
	}

	return *GameWorld;
}