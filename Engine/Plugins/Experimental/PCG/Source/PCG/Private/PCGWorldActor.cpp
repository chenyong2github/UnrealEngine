// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGWorldActor.h"

#include "PCGSubsystem.h"

#include "Engine/World.h"

APCGWorldActor::APCGWorldActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bIsEditorOnlyActor = true;
#if WITH_EDITORONLY_DATA
	bIsSpatiallyLoaded = false;
	bDefaultOutlinerExpansionState = false;
#endif
}

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