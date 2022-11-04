// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosFlesh/ChaosDeformableSolverActor.h"

#include "ChaosFlesh/FleshActor.h"
#include "ChaosFlesh/ChaosDeformableSolverComponent.h"
#include "ChaosFlesh/ChaosDeformableSolverThreading.h"
#include "CoreMinimal.h"

DEFINE_LOG_CATEGORY_STATIC(LogChaosDeformableSolverActor, Log, All);

ADeformableSolverActor::ADeformableSolverActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	//UE_LOG(ADeformableSolverActor, Verbose, TEXT("ADeformableSolverActor::ADeformableSolverActor()"));
	SolverComponent = CreateDefaultSubobject<UDeformableSolverComponent>(TEXT("DeformableSolverComponent0"));
	RootComponent = SolverComponent;
}


