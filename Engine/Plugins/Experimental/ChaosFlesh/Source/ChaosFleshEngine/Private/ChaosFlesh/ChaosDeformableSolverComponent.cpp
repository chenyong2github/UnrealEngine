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

FChaosEngineDeformableCVarParams GChaosEngineDeformableCVarParams;
FAutoConsoleVariableRef CVarChaosEngineDeformableSolverbEnabled(TEXT("p.Chaos.Deformable.EnableSimulation"), GChaosEngineDeformableCVarParams.bEnableDeformableSolver, TEXT("Enable the deformable simulation. [default : true]"));

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


bool UDeformableSolverComponent::IsSimulatable() const
{
	return true;
}

bool UDeformableSolverComponent::IsSimulating(UDeformablePhysicsComponent* InComponent) const
{
	if (InComponent)
	{
		const UDeformableSolverComponent* ComponentSolver = InComponent->PrimarySolverComponent.Get();
		return ComponentSolver ==this;
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

	if (GChaosEngineDeformableCVarParams.bEnableDeformableSolver)
	{
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
}

void UDeformableSolverComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void UDeformableSolverComponent::Reset()
{
	if (GChaosEngineDeformableCVarParams.bEnableDeformableSolver)
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

		for (TObjectPtr<UDeformablePhysicsComponent>& DeformableComponent : DeformableComponents)
		{
			if( DeformableComponent )
			{
				if (IsSimulating(DeformableComponent))
				{
					AddDeformableProxy(DeformableComponent);
				}
			}
		}
	}
}

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
		for (TObjectPtr<UDeformablePhysicsComponent>& DeformableComponent : DeformableComponents)
		{
			if (DeformableComponent)
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
			for (TObjectPtr<UDeformablePhysicsComponent>& DeformableComponent : DeformableComponents)
			{
				if (DeformableComponent)
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







