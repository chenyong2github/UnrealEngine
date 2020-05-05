// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/ChaosEventListenerComponent.h"
#include "PhysicsSolver.h"
#include "Chaos/ChaosSolverActor.h"

UChaosEventListenerComponent::UChaosEventListenerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.SetTickFunctionEnable(true);
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UChaosEventListenerComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

