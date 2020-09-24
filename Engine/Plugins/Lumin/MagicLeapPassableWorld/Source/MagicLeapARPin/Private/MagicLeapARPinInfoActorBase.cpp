// Copyright Epic Games, Inc. All Rights Reserved.

#include "Debug/MagicLeapARPinInfoActorBase.h"

AMagicLeapARPinInfoActorBase::AMagicLeapARPinInfoActorBase()
: bVisibilityOverride(true)
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));

	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
	SpawnCollisionHandlingMethod = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
}
