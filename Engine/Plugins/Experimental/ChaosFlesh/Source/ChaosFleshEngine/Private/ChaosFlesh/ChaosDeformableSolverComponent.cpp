// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosFlesh/ChaosDeformableSolverComponent.h"

#include "ChaosFlesh/ChaosDeformablePhysicsComponent.h"
#include "ChaosFlesh/ChaosDeformableSolverActor.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogDeformableSolverComponentInternal, Log, All);

static TAutoConsoleVariable<int32> CVarDeformablePhysicsTickWaitForParallelDeformableTask(
	TEXT("p.ClothPhysics.WaitForParallelDeformableTask"), 0, 
	TEXT("If 1, always wait for deformable task completion in the Deformable Tick function. "\
		 "If 0, wait at end - of - frame updates instead if allowed by component settings"));

UDeformableSolverComponent::UDeformableSolverComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, Solver()
{
	PrimaryComponentTick.bCanEverTick = true;
	bTickInEditor = false;

	UpdateTickGroup();
}

UDeformableSolverComponent::~UDeformableSolverComponent()
{
}

void UDeformableSolverComponent::UpdateTickGroup()
{
	//
	// OR THIS SolverData->PreSolveHandle  = Solver->AddPreAdvanceCallback(FSolverPreAdvance::FDelegate::CreateUObject(this, &AChaosCacheManager::HandlePreSolve, Solver));
	// see : CacheManagerActor.cpp::348
	
	//
	if (ExecutionModel == EDeformableExecutionModel::Chaos_Deformable_PrePhysics)
	{
		PrimaryComponentTick.TickGroup = TG_PrePhysics;
		DeformableEndTickFunction.TickGroup = TG_PrePhysics;
	}
	else if (ExecutionModel == EDeformableExecutionModel::Chaos_Deformable_PostPhysics)
	{
		PrimaryComponentTick.TickGroup = TG_PostPhysics;
		DeformableEndTickFunction.TickGroup = TG_LastDemotable;
	}
	else //EDeformableExecutionModel::Chaos_Deformable_DuringPhysics
	{
		PrimaryComponentTick.TickGroup = TG_PrePhysics;
		DeformableEndTickFunction.TickGroup = TG_PostPhysics;
	}

	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEvenWhenPaused = false;

	if (bDoThreadedAdvance)
	{
		DeformableEndTickFunction.bCanEverTick = true;
		DeformableEndTickFunction.bStartWithTickEnabled = true;
	}
	else
	{
		DeformableEndTickFunction.bCanEverTick = false;
		DeformableEndTickFunction.bStartWithTickEnabled = false;
	}
}


UDeformableSolverComponent::FDeformableSolver::FGameThreadAccess
UDeformableSolverComponent::GameThreadAccess()
{
	return FDeformableSolver::FGameThreadAccess(Solver.Get(), Chaos::Softs::FGameThreadAccessor());
}


#if WITH_EDITOR
void UDeformableSolverComponent::PreEditChange(FProperty* PropertyThatWillChange)
{
	Super::PreEditChange(PropertyThatWillChange);
	if (PropertyThatWillChange && PropertyThatWillChange->GetFName() == GET_MEMBER_NAME_CHECKED(UDeformableSolverComponent, DeformableActors))
	{
		PreEditChangeDeformableActors = DeformableActors;
	}
}

void UDeformableSolverComponent::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	//
	// The UDeformablePhysicsComponent and the UDeformableSolverComponent hold references to each other. 
	// If one of the attributes change, then the attribute on the other component needs to be updated. 
	//
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UDeformableSolverComponent, DeformableActors))
	{
		// process removed actors
		for (TObjectPtr<AActor>& SimulatedActor : PreEditChangeDeformableActors)
		{
			if (SimulatedActor)
			{
				if (!DeformableActors.Contains(SimulatedActor))
				{
					TArray<UDeformablePhysicsComponent*> DeformableComponentsOnActor;
					SimulatedActor->GetComponents<UDeformablePhysicsComponent>(DeformableComponentsOnActor);
					for (UDeformablePhysicsComponent* DeformableComponent : DeformableComponentsOnActor)
					{
						if (DeformableComponent->PrimarySolver == GetOwner())
						{
							DeformableComponent->PrimarySolver = nullptr;
						}
					}
				}
			}
		}
		// process added actors
		for (TObjectPtr<AActor>& SimulatedActor : DeformableActors)
		{
			if (SimulatedActor)
			{
				if (!PreEditChangeDeformableActors.Contains(SimulatedActor))
				{
					TArray<UDeformablePhysicsComponent*> DeformableComponentsOnActor;
					SimulatedActor->GetComponents<UDeformablePhysicsComponent>(DeformableComponentsOnActor);
					for (UDeformablePhysicsComponent* DeformableComponent : DeformableComponentsOnActor)
					{
						if (DeformableComponent->PrimarySolver == nullptr)
						{
							DeformableComponent->PrimarySolver = Cast<ADeformableSolverActor>(GetOwner());
						}
					}
				}
			}
		}
	}
}
#endif

bool UDeformableSolverComponent::IsSimulatable() const
{
	return true;
}

bool UDeformableSolverComponent::IsSimulating(UDeformablePhysicsComponent* InComponent) const
{
	if (InComponent)
	{
		const AActor* ComponentSolver = InComponent->PrimarySolver.Get();
		return ComponentSolver == GetOwner();
	}
	return false;
}


void UDeformableSolverComponent::UpdateDeformableEndTickState(bool bRegister)
{
	UE_LOG(LogDeformableSolverComponentInternal, Verbose, TEXT("UDeformableSolverComponent::RegiUpdateDeformableEndTickStatesterEndPhysicsTick"));
	bRegister &= PrimaryComponentTick.IsTickFunctionRegistered();
	if (bDoThreadedAdvance)
	{
		if (bRegister != DeformableEndTickFunction.IsTickFunctionRegistered())
		{
			if (bRegister)
			{
				UWorld* World = GetWorld();
				if (World->EndPhysicsTickFunction.IsTickFunctionRegistered() && SetupActorComponentTickFunction(&DeformableEndTickFunction))
				{
					DeformableEndTickFunction.DeformableSolverComponent = this;
					// Make sure our EndPhysicsTick gets called after physics simulation is finished
					if (World != nullptr)
					{
						DeformableEndTickFunction.AddPrerequisite(this, PrimaryComponentTick);
					}
				}
			}
			else
			{
				DeformableEndTickFunction.UnRegisterTickFunction();
			}
		}
	}
	else if(DeformableEndTickFunction.IsTickFunctionRegistered())
	{
		DeformableEndTickFunction.UnRegisterTickFunction();
	}

}

void UDeformableSolverComponent::BeginPlay()
{
	Super::BeginPlay();
	Reset();
}

void UDeformableSolverComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	UE_LOG(LogDeformableSolverComponentInternal, Verbose, TEXT("UDeformableSolverComponent::TickComponent"));
	TRACE_CPUPROFILER_EVENT_SCOPE(DeformableSolverComponent_TickComponent);

	UpdateTickGroup();

	UpdateDeformableEndTickState(IsSimulatable());

	UpdateFromGameThread(DeltaTime);

	if (bDoThreadedAdvance)
	{
		// see FParallelClothCompletionTask
		FGraphEventArray Prerequisites;
		Prerequisites.Add(ParallelDeformableTask);
		FGraphEventRef DeformableCompletionEvent = TGraphTask<FParallelDeformableTask>::CreateTask(&Prerequisites, ENamedThreads::GameThread).ConstructAndDispatchWhenReady(this, DeltaTime);
		ThisTickFunction->GetCompletionHandle()->DontCompleteUntil(DeformableCompletionEvent);
	}
	else
	{
		Simulate(DeltaTime);

		UpdateFromSimulation(DeltaTime);
	}


}

void UDeformableSolverComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void UDeformableSolverComponent::Reset()
{
	Solver.Reset(new FDeformableSolver({
		NumSubSteps
		, NumSolverIterations
		, FixTimeStep
		, TimeStepSize
		, CacheToFile
		, bEnableKinematics
		, bUseFloor
		, bDoSelfCollision
		, bUseGridBasedConstraints
		, GridDx
		, bDoQuasistatics
		, YoungModulus
		, bDoBlended
		, BlendedZeta
		, Damping
		, bEnableGravity
		}));

	
	for (TObjectPtr<AActor>& DeformableActor : DeformableActors)
	{
		if (DeformableActor)
		{
			TArray<UDeformablePhysicsComponent*> DeformableComponents;
			DeformableActor->GetComponents<UDeformablePhysicsComponent>(DeformableComponents);

			for (UDeformablePhysicsComponent* DeformableComponent : DeformableComponents)
			{
				if (IsSimulating(DeformableComponent))
				{
					AddDeformableProxy(DeformableComponent);
				}
			}
		}
	}
	
}

/*
* @todo(flesh) : In game additions. 
void UDeformableSolverComponent::AddDeformableActor(TObjectPtr<AActor> InActor)
{
	if (InActor)
	{
		if (!DeformableActors.Contains(InActor))
		{
			DeformableActors.Add(InActor);
		}

		if (AFleshActor* FleshActor = Cast<AFleshActor>(InActor.Get()))
		{
			TArray<UDeformablePhysicsComponent*> DeformableComponents;
			InActor->GetComponents<UDeformablePhysicsComponent>(DeformableComponents);

			for (UDeformablePhysicsComponent* DeformableComponent : DeformableComponents)
			{
				AddDeformableProxy(DeformableComponent);
			}
		}
	}
}
*/

void UDeformableSolverComponent::AddDeformableProxy(UDeformablePhysicsComponent* InComponent)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DeformableSolverComponent_AddDeformableProxy);

	if (Solver && IsSimulating(InComponent))
	{
		FDeformableSolver::FGameThreadAccess GameThreadSolver(Solver.Get(), Chaos::Softs::FGameThreadAccessor());
		if (!GameThreadSolver.HasObject(InComponent))
		{
			InComponent->AddProxy(GameThreadSolver);
		}
	}
}

void UDeformableSolverComponent::Simulate(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DeformableSolverComponent_Simulate);

	if (Solver)
	{
		// @todo(accessor) : Should be coming from the threading class. 
		FDeformableSolver::FPhysicsThreadAccess PhysicsThreadSolver(Solver.Get(), Chaos::Softs::FPhysicsThreadAccessor());
		PhysicsThreadSolver.Simulate(DeltaTime);
	}
}

void UDeformableSolverComponent::UpdateFromGameThread(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DeformableSolverComponent_UpdateFromGameThread);

	if (Solver)
	{
		FDeformableSolver::FGameThreadAccess GameThreadSolver(Solver.Get(), Chaos::Softs::FGameThreadAccessor());

		Chaos::Softs::FDeformableDataMap DataMap;
		for (TObjectPtr<AActor>& DeformableActor : DeformableActors)
		{
			if (DeformableActor)
			{
				TArray<UDeformablePhysicsComponent*> DeformableComponents;
				DeformableActor->GetComponents<UDeformablePhysicsComponent>(DeformableComponents);

				for (UDeformablePhysicsComponent* DeformableComponent : DeformableComponents)
				{
					if (IsSimulating(DeformableComponent))
					{
						if (FDataMapValue Value = DeformableComponent->NewDeformableData())
						{
							DataMap.Add(DeformableComponent, Value);
						}
					}
				}
			}
		}

		GameThreadSolver.PushInputPackage(GameThreadSolver.GetFrame(), MoveTemp(DataMap));
	}
}

void UDeformableSolverComponent::UpdateFromSimulation(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DeformableSolverComponent_UpdateFromSimulation);

	if (Solver)
	{
		FDeformableSolver::FGameThreadAccess GameThreadSolver(Solver.Get(), Chaos::Softs::FGameThreadAccessor());
		TUniquePtr<FDeformablePackage> Output(nullptr);
		while (TUniquePtr<FDeformablePackage> SolverOutput = GameThreadSolver.PullOutputPackage())
		{
			Output.Reset(SolverOutput.Release());
		}

		if (Output)
		{
			for (TObjectPtr<AActor>& DeformableActor : DeformableActors)
			{
				if (DeformableActor)
				{
					TArray<UDeformablePhysicsComponent*> DeformableComponents;
					DeformableActor->GetComponents<UDeformablePhysicsComponent>(DeformableComponents);

					for (UDeformablePhysicsComponent* DeformableComponent : DeformableComponents)
					{
						if (IsSimulating(DeformableComponent))
						{
							if (const FDataMapValue* Buffer = Output->ObjectMap.Find(DeformableComponent))
							{
								DeformableComponent->UpdateFromSimualtion(Buffer);
							}
						}
					}
				}
			}
		}
	}
}







