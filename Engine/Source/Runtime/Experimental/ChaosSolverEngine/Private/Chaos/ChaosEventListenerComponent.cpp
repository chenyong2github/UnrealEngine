// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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

const AChaosSolverActor* UChaosEventListenerComponent::GetSolverActor() const
{
	return GetTypedOuter<AChaosSolverActor>();
}

const Chaos::FPhysicsSolver* UChaosEventListenerComponent::GetSolver() const
{
	const AChaosSolverActor* A = GetSolverActor();
	return A ? A->GetSolver() : nullptr;
}

const TSharedPtr<FPhysScene_Chaos> UChaosEventListenerComponent::GetPhysicsScene() const
{
	const AChaosSolverActor* A = GetSolverActor();
	return A ? A->GetPhysicsScene() : nullptr;
}
