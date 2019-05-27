// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RuntimeVirtualTextureVolumeActorFactory.h"

#include "VT/RuntimeVirtualTextureVolume.h"

#define LOCTEXT_NAMESPACE "VirtualTexturingEditorModule"

URuntimeVirtualTextureVolumeActorFactory::URuntimeVirtualTextureVolumeActorFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DisplayName = LOCTEXT("VirtualTextureVolume_DisplayName", "Virtual Texture Volume");
	NewActorClass = ARuntimeVirtualTextureVolume::StaticClass();
	bShowInEditorQuickMenu = 1;
}

void URuntimeVirtualTextureVolumeActorFactory::PostSpawnActor(UObject* Asset, AActor* NewActor)
{
	FText ActorName = LOCTEXT("VirtualTextureVolume_DefaultActorName", "Virtual Texture");
	NewActor->SetActorLabel(ActorName.ToString());

	// Good default size to see object in editor
	NewActor->SetActorScale3D(FVector(100.f, 100.f, 1.f));

	Super::PostSpawnActor(Asset, NewActor);
}

#undef LOCTEXT_NAMESPACE
