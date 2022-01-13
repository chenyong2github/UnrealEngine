// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGWorldActor.h"

#include "PCGSubsystem.h"

#include "Engine/World.h"

void APCGWorldActor::PostLoad()
{
	Super::PostLoad();
	RegisterToSubsystem();
}

void APCGWorldActor::BeginDestroy()
{
	UnregisterFromSubsystem();
	Super::BeginDestroy();
}

#if WITH_EDITOR
APCGWorldActor* APCGWorldActor::CreatePCGWorldActor(UWorld* InWorld)
{
	check(InWorld);
	APCGWorldActor* PCGActor = InWorld->SpawnActor<APCGWorldActor>();

	// This actor should be editor only and not spatially loaded
	PCGActor->bIsEditorOnlyActor = true;
	PCGActor->bIsSpatiallyLoaded = false;

	PCGActor->RegisterToSubsystem();

	return PCGActor;
}
#endif

void APCGWorldActor::RegisterToSubsystem()
{
	UPCGSubsystem* PCGSubsystem = GetWorld() ? GetWorld()->GetSubsystem<UPCGSubsystem>() : nullptr;
	if (PCGSubsystem)
	{
		PCGSubsystem->RegisterPCGWorldActor(this);
	}
}

void APCGWorldActor::UnregisterFromSubsystem()
{
	UPCGSubsystem* PCGSubsystem = GetWorld() ? GetWorld()->GetSubsystem<UPCGSubsystem>() : nullptr;
	if (PCGSubsystem)
	{
		PCGSubsystem->UnregisterPCGWorldActor(this);
	}
}