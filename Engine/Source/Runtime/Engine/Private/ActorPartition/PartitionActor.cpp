// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActorPartition/PartitionActor.h"

#if WITH_EDITOR
#include "Components/BoxComponent.h"
#endif

APartitionActor::APartitionActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITORONLY_DATA
	, GridSize(1)
#endif
{
	USceneComponent* SceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent0"));
	RootComponent = SceneComponent;
	RootComponent->Mobility = EComponentMobility::Static;
}