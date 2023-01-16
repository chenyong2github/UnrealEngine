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
	if (PrimarySolver)
	{
		if (UDeformableSolverComponent* SolverComponent = PrimarySolver->GetDeformableSolverComponent())
		{
			FDeformableSolver::FGameThreadAccess GameThreadSolver = SolverComponent->GameThreadAccess();
			if (GameThreadSolver())
			{
				AddProxy(GameThreadSolver);
			}
		}
	}
}

void UDeformablePhysicsComponent::OnDestroyPhysicsState()
{
	Super::OnDestroyPhysicsState();
	UE_LOG(LogDeformablePhysicsComponentInternal, Log, TEXT("UDeformablePhysicsComponent::OnDestroyPhysicsState()"));
	if (PrimarySolver)
	{
		if (UDeformableSolverComponent* SolverComponent = PrimarySolver->GetDeformableSolverComponent())
		{
			FDeformableSolver::FGameThreadAccess GameThreadSolver = SolverComponent->GameThreadAccess();
			if (GameThreadSolver())
			{
				RemoveProxy(GameThreadSolver);
			}
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


#if WITH_EDITOR
void UDeformablePhysicsComponent::PreEditChange(FProperty* PropertyThatWillChange)
{
	Super::PreEditChange(PropertyThatWillChange);
	if (PropertyThatWillChange && PropertyThatWillChange->GetFName() == GET_MEMBER_NAME_CHECKED(UFleshComponent, PrimarySolver))
	{
		PreEditChangePrimarySolver = PrimarySolver;
	}
}


void UDeformablePhysicsComponent::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	//
	// The UDeformablePhysicsComponent and the UDeformableSolverComponent hold references to each other. 
	// If one of the attributes change, then the attribute on the other component needs to be updated. 
	//
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UFleshComponent, PrimarySolver))
	{
		if (AActor* Owner = Cast<AActor>(GetOwner()))
		{
			if (PrimarySolver)
			{
				if (UDeformableSolverComponent* SolverComponent = PrimarySolver->GetDeformableSolverComponent())
				{
					if (!SolverComponent->DeformableActors.Contains(Owner))
					{
						SolverComponent->DeformableActors.Add(TObjectPtr<AActor>(Owner));
					}
				}
			}
			else if (PreEditChangePrimarySolver)
			{
				bool DrivesOtherComponentsOnActor = false;
				TArray<UDeformablePhysicsComponent*> DeformableComponentsOnActor;
				Owner->GetComponents<UDeformablePhysicsComponent>(DeformableComponentsOnActor);
				for (UDeformablePhysicsComponent* DeformableComponent : DeformableComponentsOnActor)
				{
					if (DeformableComponent->PrimarySolver == PreEditChangePrimarySolver)
					{
						DrivesOtherComponentsOnActor = true;
						break;
					}
				}
				if (!DrivesOtherComponentsOnActor)
				{
					if (UDeformableSolverComponent* SolverComponent = PreEditChangePrimarySolver->GetDeformableSolverComponent())
					{
						if (SolverComponent->DeformableActors.Contains(Owner))
						{
							SolverComponent->DeformableActors.Remove(Owner);
						}
					}
				}
			}
		}
		PreEditChangePrimarySolver = nullptr;
	}
}
#endif


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
	return PrimarySolver ? PrimarySolver->GetDeformableSolverComponent() : nullptr;
}
const UDeformableSolverComponent* UDeformablePhysicsComponent::GetDeformableSolver() const
{ 
	return PrimarySolver ? PrimarySolver->GetDeformableSolverComponent() : nullptr; 
}

UDeformablePhysicsComponent::UDeformablePhysicsComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}


void UDeformablePhysicsComponent::EnableSimulation(ADeformableSolverActor* InActor)
{
	if (InActor)
	{
		if (UDeformableSolverComponent* SolverComponent = InActor->GetDeformableSolverComponent())
		{
			if (AActor* Owner = Cast<AActor>(this->GetOwner()))
			{
				PrimarySolver = InActor;
				if (!SolverComponent->DeformableActors.Contains(Owner))
				{
					SolverComponent->DeformableActors.Add(Owner);
				}
				SolverComponent->AddDeformableProxy(this);
			}
		}
	}
}








