// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGVolumeFactory.h"
#include "PCGVolume.h"

#define LOCTEXT_NAMESPACE "PCGVolumeFactory"

UPCGVolumeFactory::UPCGVolumeFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DisplayName = LOCTEXT("PCGVolumeDisplayName", "PCG Volume");
	NewActorClass = APCGVolume::StaticClass();
	bUseSurfaceOrientation = true;
}

#undef LOCTEXT_NAMESPACE