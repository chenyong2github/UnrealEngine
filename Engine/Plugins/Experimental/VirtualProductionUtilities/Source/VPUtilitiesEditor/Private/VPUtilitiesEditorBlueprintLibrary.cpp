// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "VPUtilitiesEditorBlueprintLibrary.h"
#include "VPScoutingSubsystem.h"
#include "VPUtilitiesEditorModule.h"
#include "Editor.h"

AVPEditorTickableActorBase* UVPUtilitiesEditorBlueprintLibrary::SpawnVPEditorTickableActor(UObject* ContextObject, const TSubclassOf<AVPEditorTickableActorBase> ActorClass, const FVector Location, const FRotator Rotation)
{
	if (ActorClass.Get() == nullptr)
	{
		UE_LOG(LogVPUtilitiesEditor, Warning, TEXT("VPUtilitiesEditorBlueprintLibrary::SpawnVPEditorTickableActor - The ActorClass is invalid"));
		return nullptr;
	}

	UWorld* World = ContextObject ? ContextObject->GetWorld() : nullptr;
	if (World == nullptr)
	{
		UE_LOG(LogVPUtilitiesEditor, Warning, TEXT("VPUtilitiesEditorBlueprintLibrary::SpawnVPEditorTickableActor - The ContextObject is invalid."));
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AVPEditorTickableActorBase* NewActor = World->SpawnActor<AVPEditorTickableActorBase>(ActorClass.Get(), Location, Rotation, SpawnParams);
	return NewActor;
}

AVPTransientEditorTickableActorBase* UVPUtilitiesEditorBlueprintLibrary::SpawnVPTransientEditorTickableActor(UObject* ContextObject, const TSubclassOf<AVPTransientEditorTickableActorBase> ActorClass, const FVector Location, const FRotator Rotation)
{
	if (ActorClass.Get() == nullptr)
	{
		UE_LOG(LogVPUtilitiesEditor, Warning, TEXT("VPUtilitiesEditorBlueprintLibrary::SpawnVPTransientEditorTickableActor - The ActorClass is invalid"));
		return nullptr;
	}

	UWorld* World = ContextObject ? ContextObject->GetWorld() : nullptr;
	if (World == nullptr)
	{
		UE_LOG(LogVPUtilitiesEditor, Warning, TEXT("VPUtilitiesEditorBlueprintLibrary::SpawnVPTransientEditorTickableActor - The ContextObject is invalid."));
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AVPTransientEditorTickableActorBase* NewActor = World->SpawnActor<AVPTransientEditorTickableActorBase>(ActorClass.Get(), Location, Rotation, SpawnParams);
	return NewActor;
}
