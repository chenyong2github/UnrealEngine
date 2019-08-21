// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Chaos/ChaosSolverActor.h"
#include "UObject/ConstructorHelpers.h"
#include "PhysicsSolver.h"
#include "ChaosModule.h"

#include "Components/BillboardComponent.h"
#include "EngineUtils.h"
#include "ChaosSolversModule.h"
#include "Chaos/ChaosGameplayEventDispatcher.h"
#include "Chaos/Framework/DebugSubstep.h"

//DEFINE_LOG_CATEGORY_STATIC(AFA_Log, NoLogging, All);

#if INCLUDE_CHAOS
#ifndef CHAOS_WITH_PAUSABLE_SOLVER
#define CHAOS_WITH_PAUSABLE_SOLVER 1
#endif
#else
#define CHAOS_WITH_PAUSABLE_SOLVER 0
#endif

#if CHAOS_DEBUG_SUBSTEP
#include "HAL/IConsoleManager.h"
#include "Chaos/Utilities.h"

class FChaosSolverActorConsoleObjects final
{
public:
	FChaosSolverActorConsoleObjects()
		: ConsoleCommands()
	{
		// Register console command
		ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("p.Chaos.Solver.List"),
			TEXT("List all registered solvers. The solver name can then be used by the p.Chaos.Solver.Pause or p.Chaos.Solver.Substep commands."),
			FConsoleCommandDelegate::CreateRaw(this, &FChaosSolverActorConsoleObjects::List),
			ECVF_Cheat));

		ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("p.Chaos.Solver.Pause"),
			TEXT("Debug pause the specified solver."),
			FConsoleCommandWithArgsDelegate::CreateRaw(this, &FChaosSolverActorConsoleObjects::Pause),
			ECVF_Cheat));

		ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("p.Chaos.Solver.Step"),
			TEXT("Debug step the specified solver."),
			FConsoleCommandWithArgsDelegate::CreateRaw(this, &FChaosSolverActorConsoleObjects::Step),
			ECVF_Cheat));

		ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("p.Chaos.Solver.Substep"),
			TEXT("Debug substep the specified solver."),
			FConsoleCommandWithArgsDelegate::CreateRaw(this, &FChaosSolverActorConsoleObjects::Substep),
			ECVF_Cheat));
	}

	~FChaosSolverActorConsoleObjects()
	{
		for (IConsoleObject* ConsoleCommand: ConsoleCommands)
		{
			IConsoleManager::Get().UnregisterConsoleObject(ConsoleCommand);
		}
	}

	void AddSolver(const FString& Name, AChaosSolverActor* SolverActor) { SolverActors.Add(Name, SolverActor); }
	void RemoveSolver(const FString& Name) { SolverActors.Remove(Name); }

private:
	void List()
	{
		for (auto SolverActor: SolverActors)
		{
			const Chaos::FPhysicsSolver* const Solver = SolverActor.Value->GetSolver();
			if (Solver)
			{
				UE_LOG(LogChaosDebug, Display, TEXT("%s (%d objects)"), *SolverActor.Key, Solver->GetNumPhysicsProxies());
			}
		}
	}

	void Pause(const TArray<FString>& Args)
	{
		AChaosSolverActor* const* SolverActor;
		Chaos::FPhysicsSolver* Solver;
		switch (Args.Num())
		{
		default:
			break;  // Invalid arguments
		case 1:
			if ((SolverActor = SolverActors.Find(Args[0])) != nullptr &&
				(Solver = (*SolverActor)->GetSolver()) != nullptr)
			{
				UE_LOG(LogChaosDebug, Display, TEXT("%d"), (*SolverActor)->ChaosDebugSubstepControl.bPause);
				return;
			}
			break;  // Invalid arguments
		case 2:
			if ((SolverActor = SolverActors.Find(Args[0])) != nullptr &&
				(Solver = (*SolverActor)->GetSolver()) != nullptr)
			{
#if TODO_REIMPLEMENT_DEBUG_SUBSTEP
				if (Args[1] == TEXT("0"))
				{
					Solver->GetDebugSubstep().Enable(false);
					(*SolverActor)->ChaosDebugSubstepControl.bPause = false;
#if WITH_EDITOR
					(*SolverActor)->ChaosDebugSubstepControl.OnPauseChanged.ExecuteIfBound();
#endif
					return;
				}
				else if (Args[1] == TEXT("1"))
				{
					Solver->GetDebugSubstep().Enable(true);
					(*SolverActor)->ChaosDebugSubstepControl.bPause = true;
#if WITH_EDITOR
					(*SolverActor)->ChaosDebugSubstepControl.OnPauseChanged.ExecuteIfBound();
#endif
					return;
				}
#endif
			}
			break;  // Invalid arguments
		}
		UE_LOG(LogChaosDebug, Display, TEXT("Invalid arguments."));
		UE_LOG(LogChaosDebug, Display, TEXT("Usage:"));
		UE_LOG(LogChaosDebug, Display, TEXT("  p.Chaos.Solver.Pause [SolverName] [0|1|]"));
		UE_LOG(LogChaosDebug, Display, TEXT("  SolverName  The Id name of the solver as shown by p.Chaos.Solver.List"));
		UE_LOG(LogChaosDebug, Display, TEXT("  0|1|        Use either 0 to unpause, 1 to pause, or nothing to query"));
		UE_LOG(LogChaosDebug, Display, TEXT("Example: p.Chaos.Solver.Pause ChaosSolverActor_3 1"));
	}

	void Step(const TArray<FString>& Args)
	{
#if TODO_REIMPLEMENT_DEBUG_SUBSTEP
		AChaosSolverActor* const* SolverActor;
		Chaos::FPhysicsSolver* Solver;
		switch (Args.Num())
		{
		default:
			break;  // Invalid arguments
		case 1:
			if ((SolverActor = SolverActors.Find(Args[0])) != nullptr &&
				(Solver = (*SolverActor)->GetSolver()) != nullptr)
			{
				Solver->GetDebugSubstep().ProgressToStep();
				return;
			}
			break;  // Invalid arguments
		}
		UE_LOG(LogChaosDebug, Display, TEXT("Invalid arguments."));
		UE_LOG(LogChaosDebug, Display, TEXT("Usage:"));
		UE_LOG(LogChaosDebug, Display, TEXT("  p.Chaos.Solver.Step [SolverName]"));
		UE_LOG(LogChaosDebug, Display, TEXT("  SolverName  The Id name of the solver as shown by p.Chaos.Solver.List"));
		UE_LOG(LogChaosDebug, Display, TEXT("Example: p.Chaos.Solver.Step ChaosSolverActor_3"));
#endif
	}

	void Substep(const TArray<FString>& Args)
	{
#if TODO_REIMPLEMENT_DEBUG_SUBSTEP
		AChaosSolverActor* const* SolverActor;
		Chaos::FPhysicsSolver* Solver;
		switch (Args.Num())
		{
		default:
			break;  // Invalid arguments
		case 1:
			if ((SolverActor = SolverActors.Find(Args[0])) != nullptr &&
				(Solver = (*SolverActor)->GetSolver()) != nullptr)
			{
				Solver->GetDebugSubstep().ProgressToSubstep();
				return;
			}
			break;  // Invalid arguments
		}
		UE_LOG(LogChaosDebug, Display, TEXT("Invalid arguments."));
		UE_LOG(LogChaosDebug, Display, TEXT("Usage:"));
		UE_LOG(LogChaosDebug, Display, TEXT("  p.Chaos.Solver.Substep [SolverName]"));
		UE_LOG(LogChaosDebug, Display, TEXT("  SolverName  The Id name of the solver as shown by p.Chaos.Solver.List"));
		UE_LOG(LogChaosDebug, Display, TEXT("Example: p.Chaos.Solver.Substep ChaosSolverActor_3"));
#endif
	}

private:
	TArray<IConsoleObject*> ConsoleCommands;
	TMap<FString, AChaosSolverActor*> SolverActors;
};
static TUniquePtr<FChaosSolverActorConsoleObjects> ChaosSolverActorConsoleObjects;
#endif  // #if CHAOS_DEBUG_SUBSTEP

AChaosSolverActor::AChaosSolverActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, TimeStepMultiplier(1.f)
	, CollisionIterations(1)
	, PushOutIterations(3)
	, PushOutPairIterations(2)
	, ClusterConnectionFactor(1.0)
	, ClusterUnionConnectionType(EClusterConnectionTypeEnum::Chaos_DelaunayTriangulation)
	, DoGenerateCollisionData(true)
	, DoGenerateBreakingData(true)
	, DoGenerateTrailingData(true)
	, bHasFloor(true)
	, FloorHeight(0.f)
	, MassScale(1.f)
	, ChaosDebugSubstepControl()
{
#if INCLUDE_CHAOS
	// @question(Benn) : Does this need to be created on the Physics thread using a queued command?
	PhysScene = MakeShareable(new FPhysScene_Chaos(this));
	Solver = PhysScene->GetSolver();
	// @todo: This should be gated by a parameter
	Solver->GetEvolution()->AddForceFunction(Chaos::Utilities::GetRigidsGravityFunction(Chaos::TVector<float, 3>(0, 0, -1), 980.f));
#endif
	// Ticking setup for collision/breaking notifies
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	PrimaryActorTick.bCanEverTick = INCLUDE_CHAOS ? true : false;
	PrimaryActorTick.bStartWithTickEnabled = INCLUDE_CHAOS ? true : false;;

	/*
	* Display icon in the editor
	*/
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		// A helper class object we use to find target UTexture2D object in resource package
		ConstructorHelpers::FObjectFinderOptional<UTexture2D> SolverTextureObject;

		// Icon sprite category name
		FName ID_Solver;

		// Icon sprite display name
		FText NAME_Solver;

		FConstructorStatics()
			// Use helper class object to find the texture
			// "/Engine/EditorResources/S_Solver" is resource path
			: SolverTextureObject(TEXT("/Engine/EditorResources/S_Solver.S_Solver"))
			, ID_Solver(TEXT("Solver"))
			, NAME_Solver(NSLOCTEXT("SpriteCategory", "Solver", "Solver"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	// We need a scene component to attach Icon sprite
	USceneComponent* SceneComponent = ObjectInitializer.CreateDefaultSubobject<USceneComponent>(this, TEXT("SceneComp"));
	RootComponent = SceneComponent;
	RootComponent->Mobility = EComponentMobility::Static;

#if WITH_EDITORONLY_DATA
	SpriteComponent = ObjectInitializer.CreateEditorOnlyDefaultSubobject<UBillboardComponent>(this, TEXT("Sprite"));
	if (SpriteComponent)
	{
		SpriteComponent->Sprite = ConstructorStatics.SolverTextureObject.Get();		// Get the sprite texture from helper class object
		SpriteComponent->SpriteInfo.Category = ConstructorStatics.ID_Solver;		// Assign sprite category name
		SpriteComponent->SpriteInfo.DisplayName = ConstructorStatics.NAME_Solver;	// Assign sprite display name
		SpriteComponent->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
		SpriteComponent->Mobility = EComponentMobility::Static;
	}
#endif // WITH_EDITORONLY_DATA

	GameplayEventDispatcherComponent = ObjectInitializer.CreateDefaultSubobject<UChaosGameplayEventDispatcher>(this, TEXT("GameplayEventDispatcher"));
}

void AChaosSolverActor::BeginPlay()
{
	Super::BeginPlay();

#if INCLUDE_CHAOS
	Chaos::IDispatcher* PhysDispatcher = PhysScene->GetDispatcher();
	if (PhysDispatcher)
	{
		PhysDispatcher->EnqueueCommandImmediate(Solver,
			[InTimeStepMultiplier = TimeStepMultiplier
			, InCollisionIterations = CollisionIterations
			, InPushOutIterations = PushOutIterations
			, InPushOutPairIterations = PushOutPairIterations
			, InClusterConnectionFactor = ClusterConnectionFactor
			, InClusterUnionConnectionType = ClusterUnionConnectionType
			, InDoGenerateCollisionData = DoGenerateCollisionData
			, InDoGenerateBreakingData = DoGenerateBreakingData
			, InDoGenerateTrailingData = DoGenerateTrailingData
			, InCollisionFilterSettings = CollisionFilterSettings
			, InBreakingFilterSettings = BreakingFilterSettings
			, InTrailingFilterSettings = TrailingFilterSettings
			, InHasFloor = bHasFloor
			, InFloorHeight = FloorHeight
			, InMassScale = MassScale]
		(Chaos::FPhysicsSolver* InSolver)
		{
#if TODO_REIMPLEMENT_SOLVER_SETTINGS_ACCESSORS
			InSolver->SetTimeStepMultiplier(InTimeStepMultiplier);
			InSolver->SetIterations(InCollisionIterations);
			InSolver->SetPushOutIterations(InPushOutIterations);
			InSolver->SetPushOutPairIterations(InPushOutPairIterations);
			InSolver->SetClusterConnectionFactor(InClusterConnectionFactor);
			InSolver->SetClusterUnionConnectionType((Chaos::FClusterCreationParameters<float>::EConnectionMethod)InClusterUnionConnectionType);
#endif
			InSolver->SetGenerateCollisionData(InDoGenerateCollisionData);
			InSolver->SetGenerateBreakingData(InDoGenerateBreakingData);
			InSolver->SetGenerateTrailingData(InDoGenerateTrailingData);
			InSolver->SetCollisionFilterSettings(InCollisionFilterSettings);
			InSolver->SetBreakingFilterSettings(InBreakingFilterSettings);
			InSolver->SetTrailingFilterSettings(InTrailingFilterSettings);
			InSolver->SetHasFloor(InHasFloor);
			InSolver->SetFloorHeight(InFloorHeight);
#if TODO_REIMPLEMENT_SOLVER_SETTINGS_ACCESSORS
			InSolver->SetMassScale(InMassScale);
#endif
			InSolver->SetEnabled(true);
#if TODO_REIMPLEMENT_SOLVER_SETTINGS_ACCESSORS
#if CHAOS_WITH_PAUSABLE_SOLVER
			InSolver->SetPaused(false);
#endif  // #if CHAOS_WITH_PAUSABLE_SOLVER
#endif  // #if TODO_REIMPLEMENT_SOLVER_SETTINGS_ACCESSORS
		});
	}
#endif

#if TODO_REIMPLEMENT_DEBUG_SUBSTEP
#if CHAOS_DEBUG_SUBSTEP
	if (!ChaosSolverActorConsoleObjects)
	{
		ChaosSolverActorConsoleObjects = MakeUnique<FChaosSolverActorConsoleObjects>();
	}
	ChaosSolverActorConsoleObjects->AddSolver(GetName(), this);
#if WITH_EDITOR
	if (ChaosDebugSubstepControl.bPause)
	{
		Solver->GetDebugSubstep().Enable(true);
	}
#endif  // #if WITH_EDITOR
#endif  // #if CHAOS_DEBUG_SUBSTEP
#endif  // #if TODO_REIMPLEMENT_DEBUG_SUBSTEP
}

void AChaosSolverActor::EndPlay(EEndPlayReason::Type ReasonEnd)
{
#if INCLUDE_CHAOS
	Chaos::IDispatcher* PhysDispatcher = PhysScene->GetDispatcher();
	if (PhysDispatcher)
	{
		PhysDispatcher->EnqueueCommandImmediate(Solver, [](Chaos::FPhysicsSolver* InSolver)
		{
			// #TODO BG - We should really reset the solver here but the current reset function
			// is really heavy handed and clears out absolutely everything. Ideally we want to keep
			// all of the solver physics proxies and revert to a state before the very first tick
			InSolver->SetEnabled(false);
		});
	}
#endif
#if TODO_REIMPLEMENT_DEBUG_SUBSTEP
#if CHAOS_DEBUG_SUBSTEP
	ChaosSolverActorConsoleObjects->RemoveSolver(GetName());
#endif  // #if CHAOS_DEBUG_SUBSTEP
#endif  // #if TODO_REIMPLEMENT_DEBUG_SUBSTEP
}

void AChaosSolverActor::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

#if INCLUDE_CHAOS
	UWorld* const W = GetWorld(); 
	if (W && !W->PhysicsScene_Chaos)
	{
		SetAsCurrentWorldSolver();
	}
#endif
}

void AChaosSolverActor::SetAsCurrentWorldSolver()
{
#if INCLUDE_CHAOS
	UWorld* const W = GetWorld();
	if (W)
	{
		W->PhysicsScene_Chaos = PhysScene;
	}
#endif
}

void AChaosSolverActor::SetSolverActive(bool bActive)
{
#if INCLUDE_CHAOS
	if(Solver && PhysScene)
	{
		Chaos::IDispatcher* Dispatcher = PhysScene->GetDispatcher();

		if(Dispatcher)
		{
			Dispatcher->EnqueueCommandImmediate(Solver, [bShouldBeActive = bActive](Chaos::FPhysicsSolver* InSolver)
			{
				InSolver->SetEnabled(bShouldBeActive);
			});
		}
	}
#endif
}

#if WITH_EDITOR
void AChaosSolverActor::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

#if INCLUDE_CHAOS
	if (Solver && PropertyChangedEvent.Property)
	{
		Chaos::IDispatcher* PhysDispatcher = PhysScene->GetDispatcher();
		if (PhysDispatcher)
		{
#if TODO_REIMPLEMENT_TIMESTEP_MULTIPLIER
			if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(AChaosSolverActor, TimeStepMultiplier))
			{
				PhysDispatcher->EnqueueCommandImmediate(Solver, [InTimeStepMultiplier = TimeStepMultiplier]
				(Chaos::FPhysicsSolver* InSolver)
				{
					InSolver->SetTimeStepMultiplier(InTimeStepMultiplier);
				});
			}
			else if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(AChaosSolverActor, CollisionIterations))
			{
				PhysDispatcher->EnqueueCommandImmediate(Solver, [InCollisionIterations = CollisionIterations]
				(Chaos::FPhysicsSolver* InSolver)
				{
					InSolver->SetIterations(InCollisionIterations);
				});
			}
			else if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(AChaosSolverActor, PushOutIterations))
			{
				PhysDispatcher->EnqueueCommandImmediate(Solver, [InPushOutIterations = PushOutIterations]
				(Chaos::FPhysicsSolver* InSolver)
				{
					InSolver->SetPushOutIterations(InPushOutIterations);
				});
			}
			else if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(AChaosSolverActor, PushOutPairIterations))
			{
				PhysDispatcher->EnqueueCommandImmediate(Solver, [InPushOutPairIterations = PushOutPairIterations]
				(Chaos::FPhysicsSolver* InSolver)
				{
					InSolver->SetPushOutPairIterations(InPushOutPairIterations);
				});
			}
			else
#endif
			if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(AChaosSolverActor, DoGenerateCollisionData))
			{
				PhysDispatcher->EnqueueCommandImmediate(Solver, [InDoGenerateCollisionData = DoGenerateCollisionData]
				(Chaos::FPhysicsSolver* InSolver)
				{
					InSolver->SetGenerateCollisionData(InDoGenerateCollisionData);
				});
			}
			else if (PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(AChaosSolverActor, CollisionFilterSettings))
			{
				PhysDispatcher->EnqueueCommandImmediate(Solver, [InCollisionFilterSettings = CollisionFilterSettings]
				(Chaos::FPhysicsSolver* InSolver)
				{
					InSolver->SetCollisionFilterSettings(InCollisionFilterSettings);
				});
			}
			else if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(AChaosSolverActor, DoGenerateBreakingData))
			{
				PhysDispatcher->EnqueueCommandImmediate(Solver, [InDoGenerateBreakingData = DoGenerateBreakingData]
				(Chaos::FPhysicsSolver* InSolver)
				{
					InSolver->SetGenerateBreakingData(InDoGenerateBreakingData);
				});
			}
			else if (PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(AChaosSolverActor, BreakingFilterSettings))
			{
				PhysDispatcher->EnqueueCommandImmediate(Solver, [InBreakingFilterSettings = BreakingFilterSettings]
				(Chaos::FPhysicsSolver* InSolver)
				{
					InSolver->SetBreakingFilterSettings(InBreakingFilterSettings);
				});
			}
			else if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(AChaosSolverActor, DoGenerateTrailingData))
			{
				PhysDispatcher->EnqueueCommandImmediate(Solver, [InDoGenerateTrailingData = DoGenerateTrailingData]
				(Chaos::FPhysicsSolver* InSolver)
				{
					InSolver->SetGenerateTrailingData(InDoGenerateTrailingData);
				});
			}
			else if (PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(AChaosSolverActor, TrailingFilterSettings))
			{
				PhysDispatcher->EnqueueCommandImmediate(Solver, [InTrailingFilterSettings = TrailingFilterSettings]
				(Chaos::FPhysicsSolver* InSolver)
				{
					InSolver->SetTrailingFilterSettings(InTrailingFilterSettings);
				});
			}
			else if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(AChaosSolverActor, bHasFloor))
			{
				PhysDispatcher->EnqueueCommandImmediate(Solver, [InHasFloor = bHasFloor]
				(Chaos::FPhysicsSolver* InSolver)
				{
					InSolver->SetHasFloor(InHasFloor);
				});
			}
			else if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(AChaosSolverActor, FloorHeight))
			{
				PhysDispatcher->EnqueueCommandImmediate(Solver, [InFloorHeight = FloorHeight]
				(Chaos::FPhysicsSolver* InSolver)
				{
					InSolver->SetFloorHeight(InFloorHeight);
				});
			}
#if TODO_REIMPLEMENT_TIMESTEP_MULTIPLIER
			else if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(AChaosSolverActor, MassScale))
			{
				PhysDispatcher->EnqueueCommandImmediate(Solver, [InMassScale = MassScale]
				(Chaos::FPhysicsSolver* InSolver)
				{
					InSolver->SetMassScale(InMassScale);
				});
			}
#endif
		}
	}
#endif

#if CHAOS_DEBUG_SUBSTEP
#if TODO_REIMPLEMENT_DEBUG_SUBSTEP
	if (Solver && PropertyChangedEvent.Property)
	{
		if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(FChaosDebugSubstepControl, bPause))
		{
			if (HasActorBegunPlay())
			{
				Solver->GetDebugSubstep().Enable(ChaosDebugSubstepControl.bPause);
			}
		}
		else if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(FChaosDebugSubstepControl, bSubstep))
		{
			if (HasActorBegunPlay())
			{
				Solver->GetDebugSubstep().ProgressToSubstep();
			}
			ChaosDebugSubstepControl.bSubstep = false;
		}
		else if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(FChaosDebugSubstepControl, bStep))
		{
			if (HasActorBegunPlay())
			{
				Solver->GetDebugSubstep().ProgressToStep();
			}
			ChaosDebugSubstepControl.bStep = false;
		}
	}
#endif
#endif
}

#if TODO_REIMPLEMENT_SERIALIZATION_FOR_PERF_TEST
#if !UE_BUILD_SHIPPING
void SerializeForPerfTest(const TArray< FString >&, UWorld* World, FOutputDevice&)
{
#if INCLUDE_CHAOS
	UE_LOG(LogChaos, Log, TEXT("Serializing for perf test:"));
	
	const FString FileName(TEXT("ChaosPerf"));
	//todo(mlentine): use this once chaos solver actors are in
	for (TActorIterator<AChaosSolverActor> Itr(World); Itr; ++Itr)
	{
		Chaos::FPhysicsSolver* Solver = Itr->GetSolver();
		FChaosSolversModule::GetModule()->GetDispatcher()->EnqueueCommandImmediate(Solver, [FileName](Chaos::FPhysicsSolver* SolverIn) { SolverIn->SerializeForPerfTest(FileName); });
	}
#endif
}

FAutoConsoleCommand SerializeForPerfTestCommand(TEXT("p.SerializeForPerfTest"), TEXT(""), FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&SerializeForPerfTest));
#endif
#endif

#endif

