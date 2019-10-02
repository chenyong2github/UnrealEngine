// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Movement/ParametricMovement.h"
#include "NetworkSimulationModelDebugger.h"
#include "HAL/IConsoleManager.h"
#include "NetworkSimulationGlobalManager.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"

DEFINE_LOG_CATEGORY_STATIC(LogParametricMovement, Log, All);

namespace ParametricMoverCVars
{
static int32 UseVLogger = 1;
static FAutoConsoleVariableRef CVarUseVLogger(TEXT("parametricmover.Debug.UseUnrealVLogger"),
	UseVLogger,	TEXT("Use Unreal Visual Logger\n"),	ECVF_Default);

static int32 UseDrawDebug = 1;
static FAutoConsoleVariableRef CVarUseDrawDebug(TEXT("parametricmover.Debug.UseDrawDebug"),
	UseVLogger,	TEXT("Use built in DrawDebug* functions for visual logging\n"), ECVF_Default);

static float DrawDebugDefaultLifeTime = 30.f;
static FAutoConsoleVariableRef CVarDrawDebugDefaultLifeTime(TEXT("parametricmover.Debug.UseDrawDebug.DefaultLifeTime"),
	DrawDebugDefaultLifeTime, TEXT("Use built in DrawDebug* functions for visual logging"), ECVF_Default);


static int32 SimulatedProxyBufferSize = 4;
static FAutoConsoleVariableRef CVarSimulatedProxyBufferSize(TEXT("parametricmover.SimulatedProxyBufferSize"),
	DrawDebugDefaultLifeTime, TEXT(""), ECVF_Default);

static int32 FixStep = 0;
static FAutoConsoleVariableRef CVarFixStepMS(TEXT("parametricmover.FixStep"),
	DrawDebugDefaultLifeTime, TEXT("If > 0, will use fix step"), ECVF_Default);

}

// -------------------------------------------------------------------------------------------------------------------------------
// ParametricMovement
// -------------------------------------------------------------------------------------------------------------------------------

namespace ParametricMovement
{
	const FName FMovementSimulation::GroupName(TEXT("Parametric"));

	void FMovementSimulation::Update(IMovementDriver* Driver, const float DeltaSeconds, const FInputCmd& InputCmd, const FMoveState& InputState, FMoveState& OutputState, const FAuxState& AuxState)
	{
		IBaseMovementDriver& BaseMovementDriver = Driver->GetBaseMovementDriver();
		//const float DeltaSeconds = SimTimeDeltaMS.ToRealTimeSeconds();

		// Advance parametric time. This won't always be linear: we could loop, rewind/bounce, etc
		const float InputPlayRate = InputCmd.PlayRate.Get(InputState.PlayRate); // Returns InputCmds playrate if set, else returns previous state's playrate		
		Driver->AdvanceParametricTime(InputState.Position, InputPlayRate, OutputState.Position, OutputState.PlayRate, DeltaSeconds);

		// We have our time that we should be at. We just need to move primitive component to that position.
		// Again, note that we expect this cannot fail. We move like this so that it can push things, but we don't expect failure.
		// (I think it would need to be a different simulation that supports this as a failure case. The tricky thing would be working out
		// the output Position in the case where a Move is blocked (E.g, you move 50% towards the desired location)

		FTransform StartingTransform;
		Driver->MapTimeToTransform(InputState.Position, StartingTransform);

		FTransform NewTransform;
		Driver->MapTimeToTransform(OutputState.Position, NewTransform);

		FHitResult Hit(1.f);

		FVector Delta = NewTransform.GetLocation() - StartingTransform.GetLocation();
		BaseMovementDriver.MoveUpdatedComponent(Delta, NewTransform.GetRotation(), true, &Hit, ETeleportType::None);

		if (Hit.IsValidBlockingHit())
		{
			FTransform ActualTransform = BaseMovementDriver.GetUpdateComponentTransform();
			FVector ActualDelta = NewTransform.GetLocation() - ActualTransform.GetLocation();
			UE_LOG(LogParametricMovement, Warning, TEXT("Blocking hit occurred when trying to move parametric mover. ActualDelta: %s"), *ActualDelta.ToString());
		}
	}

	void FMoveState::VisualLog(const FVisualLoggingParameters& Parameters, IMovementDriver* Driver, IMovementDriver* LogDriver) const
	{
		IBaseMovementDriver& BaseMovementDriver = Driver->GetBaseMovementDriver();
		IBaseMovementDriver& LogMovementDriver = LogDriver->GetBaseMovementDriver();

		FTransform Transform;
		Driver->MapTimeToTransform(Position, Transform);

		IBaseMovementDriver::FDrawDebugParams DrawParams(Parameters, &LogMovementDriver);
		DrawParams.Transform = Transform;
		DrawParams.InWorldText = LexToString(Parameters.Keyframe);
		DrawParams.LogText = FString::Printf(TEXT("[%d] %s. Position: %.4f. Location: %s. Rotation: %s"), Parameters.Keyframe, *LexToString(Parameters.Context), Position, *Transform.GetLocation().ToString(), *Transform.GetRotation().Rotator().ToString());

		BaseMovementDriver.DrawDebug(DrawParams);
	}
};


// -------------------------------------------------------------------------------------------------------------------------------
// UParametricMovementComponent
// -------------------------------------------------------------------------------------------------------------------------------

UParametricMovementComponent::UParametricMovementComponent()
{

}

INetworkSimulationModel* UParametricMovementComponent::InstantiateNetworkSimulation()
{
	if (bDisableParametricMovementSimulation)
	{
		return nullptr;
	}

	if (ParametricMoverCVars::FixStep > 0)
	{
		auto *NewSim = new ParametricMovement::FMovementSystem<16>(this);
		NewSim->RepProxy_Simulated.bAllowSimulatedExtrapolation = !bEnableInterpolation;
		DO_NETSIM_MODEL_DEBUG(FNetworkSimulationModelDebuggerManager::Get().RegisterNetworkSimulationModel(NewSim, GetOwner()));
		return NewSim;
	}
	
	auto *NewSim = new ParametricMovement::FMovementSystem<>(this);
	NewSim->RepProxy_Simulated.bAllowSimulatedExtrapolation = !bEnableInterpolation;
	DO_NETSIM_MODEL_DEBUG(FNetworkSimulationModelDebuggerManager::Get().RegisterNetworkSimulationModel(NewSim, GetOwner()));
	return NewSim;
}

FNetworkSimulationModelInitParameters UParametricMovementComponent::GetSimulationInitParameters(ENetRole Role)
{
	// These are reasonable defaults but may not be right for everyone
	FNetworkSimulationModelInitParameters InitParams;
	InitParams.InputBufferSize = 32; //Role != ROLE_SimulatedProxy ? 32 : 32;  // Fixme.. not good
	InitParams.SyncedBufferSize = Role != ROLE_AutonomousProxy ? ParametricMoverCVars::SimulatedProxyBufferSize : 32;
	InitParams.AuxBufferSize = Role != ROLE_AutonomousProxy ? 2 : 32;
	InitParams.DebugBufferSize = 32;
	InitParams.HistoricBufferSize = 128;
	return InitParams;
}

void UParametricMovementComponent::BeginPlay()
{
	Super::BeginPlay();
	CachedStartingTransform = UpdatedComponent->GetComponentToWorld();
}

void UParametricMovementComponent::InitSyncState(ParametricMovement::FMoveState& OutSyncState) const
{
	// In this case, we just default to the 0 position. Maybe this could be a starting variable set on the component.
	OutSyncState.Position = 0.f;
	OutSyncState.PlayRate = 0.f;
}

void UParametricMovementComponent::FinalizeFrame(const ParametricMovement::FMoveState& SyncState)
{
	FTransform NewTransform;
	MapTimeToTransform(SyncState.Position, NewTransform);

	check(UpdatedComponent);
	UpdatedComponent->SetWorldTransform(NewTransform, false, nullptr, ETeleportType::TeleportPhysics);
}

FString UParametricMovementComponent::GetDebugName() const
{
	return FString::Printf(TEXT("ParametricMovement. %s. %s"), *UEnum::GetValueAsString(TEXT("Engine.ENetRole"), GetOwnerRole()), *GetName());
}
const UObject* UParametricMovementComponent::GetVLogOwner() const
{
	return GetOwner();
}

void UParametricMovementComponent::AdvanceParametricTime(const float InPosition, const float InPlayRate, float &OutPosition, float& OutPlayRate, const float DeltaTimeSeconds) const
{
	// Real simple oscillation for now
	OutPosition = InPosition + (InPlayRate * DeltaTimeSeconds);
	OutPlayRate = InPlayRate;

	const float DeltaMax = OutPosition - MaxTime;
	if (DeltaMax > SMALL_NUMBER)
	{
		OutPosition = MaxTime - DeltaMax;
		OutPlayRate *= -1.f;
	}
	else
	{
		const float DeltaMin = OutPosition - MinTime;
		if (DeltaMin < SMALL_NUMBER)
		{
			OutPosition = MinTime - DeltaMin;
			OutPlayRate *= -1.f;
		}
	}
}

void UParametricMovementComponent::MapTimeToTransform(const float InPosition, FTransform& OutTransform) const
{
	const FVector Delta = ParametricDelta * InPosition;

	OutTransform = CachedStartingTransform;
	OutTransform.AddToTranslation(Delta);
}

void UParametricMovementComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bEnableDependentSimulation && NetworkSim.IsValid() && NetworkSim->GetParentSimulation() == nullptr)
	{
		// This is a very simply and generic way of finding a locally controlled network sim.
		// The long term vision for dependent simulations is more game specific and dynamic. 
		// E.g, stepping on elevators, getting close to moving platforms, etc. This example here is meant to 
		// just be a simple way of linking them up no matter where they are in relationship to each other
		UWorld* World = GetWorld();
		check(World);
		ULocalPlayer* Player = World->GetFirstLocalPlayerFromController();
		if (Player && Player->GetPlayerController(World))
		{
			APawn* Pawn = Player->GetPlayerController(World)->GetPawn();
			if (UNetworkPredictionComponent* NetComponent = Pawn->FindComponentByClass<UNetworkPredictionComponent>())
			{
				if (INetworkSimulationModel* ParentNetSim = NetComponent->GetNetworkSimulation())
				{
					NetworkSim->SetParentSimulation(ParentNetSim);
				}
			}
		}
	}

	if (ParentNetUpdateFrequency > 0.f)
	{
		GetOwner()->NetUpdateFrequency = ParentNetUpdateFrequency;
	}
	
	if (bEnableForceNetUpdate)
	{
		GetOwner()->ForceNetUpdate();
	}
}

void UParametricMovementComponent::ProduceInput(const FNetworkSimTime DeltaTimeSeconds, ParametricMovement::FInputCmd& Cmd)
{
	Cmd.PlayRate = PendingPlayRate;
	PendingPlayRate.Reset();
}