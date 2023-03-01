// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosFlesh/ChaosDeformableGameplayComponent.h"

#include "ChaosStats.h"
#include "Components/BoxComponent.h"
#include "Chaos/DebugDrawQueue.h"


#include UE_INLINE_GENERATED_CPP_BY_NAME(ChaosDeformableGameplayComponent)


//DECLARE_CYCLE_STAT(TEXT("Chaos.Deformable.UDeformableGameplayComponent.StreamStaticMeshGeometry"), STAT_ChaosDeformable_UDeformableGameplayComponent_StreamStaticMeshGeometry, STATGROUP_Chaos);

UDeformableGameplayComponent::UDeformableGameplayComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}


UDeformableGameplayComponent::~UDeformableGameplayComponent()
{
}
