// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosFlesh/ChaosDeformablePhysicsComponent.h"

#include "ChaosFlesh/ChaosDeformableSolverActor.h"
#include "ChaosFlesh/ChaosDeformableSolverComponent.h"
#include "ChaosFlesh/FleshComponent.h"


DEFINE_LOG_CATEGORY_STATIC(LogDeformablePhysicsComponentInternal, Log, All);

void UDeformablePhysicsComponent::OnCreatePhysicsState()
{
	Super::OnCreatePhysicsState();
	UE_LOG(LogDeformablePhysicsComponentInternal, Log, TEXT("UDeformablePhysicsComponent::OnCreatePhysicsState()"));
	if (PrimarySolverComponent)
	{
		FDeformableSolver::FGameThreadAccess GameThreadSolver = PrimarySolverComponent->GameThreadAccess();
		if (GameThreadSolver())
		{
			AddProxy(GameThreadSolver);
		}
	}
}

void UDeformablePhysicsComponent::OnDestroyPhysicsState()
{
	Super::OnDestroyPhysicsState();
	UE_LOG(LogDeformablePhysicsComponentInternal, Log, TEXT("UDeformablePhysicsComponent::OnDestroyPhysicsState()"));
	if (PrimarySolverComponent)
	{
		FDeformableSolver::FGameThreadAccess GameThreadSolver = PrimarySolverComponent->GameThreadAccess();
		if (GameThreadSolver())
		{
			RemoveProxy(GameThreadSolver);
		}
	}
}

bool UDeformablePhysicsComponent::ShouldCreatePhysicsState() const
{
	return true;
}
bool UDeformablePhysicsComponent::HasValidPhysicsState() const
{
	return PhysicsProxy != nullptr;
}

void UDeformablePhysicsComponent::AddProxy(Chaos::Softs::FDeformableSolver::FGameThreadAccess& GameThreadSolver)
{
	PhysicsProxy = NewProxy();
	if (PhysicsProxy)
	{
		// PhysicsProxy is created on game thread but is owned by physics thread. This is the handoff. 
		GameThreadSolver.AddProxy(PhysicsProxy);
	}
}

void UDeformablePhysicsComponent::RemoveProxy(Chaos::Softs::FDeformableSolver::FGameThreadAccess& GameThreadSolver)
{
	if (PhysicsProxy)
	{
		GameThreadSolver.RemoveProxy(PhysicsProxy);
		PhysicsProxy = nullptr; // destroyed on physics thread. 
	}
}

UDeformableSolverComponent* UDeformablePhysicsComponent::GetDeformableSolver()
{
	return PrimarySolverComponent;
}
const UDeformableSolverComponent* UDeformablePhysicsComponent::GetDeformableSolver() const
{ 
	return PrimarySolverComponent;
}

UDeformablePhysicsComponent::UDeformablePhysicsComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}


void UDeformablePhysicsComponent::EnableSimulation(UDeformableSolverComponent* DeformableSolverComponent)
{
	if (DeformableSolverComponent)
	{
		PrimarySolverComponent = DeformableSolverComponent;
		if (!DeformableSolverComponent->DeformableComponents.Contains(this))
		{
			DeformableSolverComponent->DeformableComponents.Add(this);
		}
		DeformableSolverComponent->AddDeformableProxy(this);
	}
}


void UDeformablePhysicsComponent::EnableSimulationFromActor(ADeformableSolverActor* DeformableSolverActor)
{
	if (DeformableSolverActor && DeformableSolverActor->GetDeformableSolverComponent())
	{
		PrimarySolverComponent = DeformableSolverActor->GetDeformableSolverComponent();
		if (!DeformableSolverActor->GetDeformableSolverComponent()->DeformableComponents.Contains(this))
		{
			DeformableSolverActor->GetDeformableSolverComponent()->DeformableComponents.Add(this);
		}
		DeformableSolverActor->GetDeformableSolverComponent()->AddDeformableProxy(this);
	}
}







