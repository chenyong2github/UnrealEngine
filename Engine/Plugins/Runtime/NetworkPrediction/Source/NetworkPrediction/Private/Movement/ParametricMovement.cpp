// Copyright Epic Games, Inc. All Rights Reserved.

#include "Movement/ParametricMovement.h"
#include "HAL/IConsoleManager.h"
#include "NetworkedSimulationGlobalManager.h"
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

const FName FParametricMovementNetSimModelDef::GroupName(TEXT("Parametric"));

void FParametricMovementSimulation::SimulationTick(const FNetSimTimeStep& TimeStep, const TNetSimInput<ParametricMovementBufferTypes>& Input, const TNetSimOutput<ParametricMovementBufferTypes>& Output)
{
	check(Motion != nullptr); // Must set motion mapping prior to running the simulation
	const float DeltaSeconds = TimeStep.StepMS.ToRealTimeSeconds();

	// Advance parametric time. This won't always be linear: we could loop, rewind/bounce, etc
	const float InputPlayRate = Input.Cmd.PlayRate.Get(Input.Sync.PlayRate); // Returns InputCmds playrate if set, else returns previous state's playrate		
	Motion->AdvanceParametricTime(Input.Sync.Position, InputPlayRate, Output.Sync.Position, Output.Sync.PlayRate, DeltaSeconds);

	// We have our time that we should be at. We just need to move primitive component to that position.
	// Again, note that we expect this cannot fail. We move like this so that it can push things, but we don't expect failure.
	// (I think it would need to be a different simulation that supports this as a failure case. The tricky thing would be working out
	// the output Position in the case where a Move is blocked (E.g, you move 50% towards the desired location)

	FTransform StartingTransform;
	Motion->MapTimeToTransform(Input.Sync.Position, StartingTransform);

	FTransform NewTransform;
	Motion->MapTimeToTransform(Output.Sync.Position, NewTransform);

	FHitResult Hit(1.f);

	FVector Delta = NewTransform.GetLocation() - StartingTransform.GetLocation();
	MoveUpdatedComponent(Delta, NewTransform.GetRotation(), true, &Hit, ETeleportType::None);

	if (Hit.IsValidBlockingHit())
	{
		FTransform ActualTransform = GetUpdateComponentTransform();
		FVector ActualDelta = NewTransform.GetLocation() - ActualTransform.GetLocation();
		UE_LOG(LogParametricMovement, Warning, TEXT("Blocking hit occurred when trying to move parametric mover. ActualDelta: %s"), *ActualDelta.ToString());
	}
}

// -------------------------------------------------------------------------------------------------------------------------------
// UParametricMovementComponent
// -------------------------------------------------------------------------------------------------------------------------------

UParametricMovementComponent::UParametricMovementComponent()
{

}

INetworkedSimulationModel* UParametricMovementComponent::InstantiateNetworkedSimulation()
{
	if (bDisableParametricMovementSimulation)
	{
		return nullptr;
	}

	// Simulation
	FParametricSyncState InitialSyncState;
	FParametricAuxState InitialAuxState;

	InitParametricMovementSimulation(new FParametricMovementSimulation(), InitialSyncState, InitialAuxState);

	// Model
	if (ParametricMoverCVars::FixStep > 0)
	{
		auto* NewModel = new TNetworkedSimulationModel<FParametricMovementNetSimModelDef_Fixed30hz>(ParametricMovementSimulation, this, InitialSyncState, InitialAuxState);
		InitParametricMovementNetSimModel(NewModel);
		return NewModel;
	}
	
	auto* NewModel = new TNetworkedSimulationModel<FParametricMovementNetSimModelDef>(ParametricMovementSimulation, this, InitialSyncState, InitialAuxState);
	InitParametricMovementNetSimModel(NewModel);
	return NewModel;
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
	ParametricMotion.CachedStartingTransform = UpdatedComponent->GetComponentToWorld();
}

void UParametricMovementComponent::FinalizeFrame(const FParametricSyncState& SyncState, const FParametricAuxState& AuxState)
{
	FTransform NewTransform;
	ParametricMotion.MapTimeToTransform(SyncState.Position, NewTransform);

	check(UpdatedComponent);
	UpdatedComponent->SetWorldTransform(NewTransform, false, nullptr, ETeleportType::TeleportPhysics);
}

FString UParametricMovementComponent::GetDebugName() const
{
	return FString::Printf(TEXT("ParametricMovement. %s. %s"), *UEnum::GetValueAsString(TEXT("Engine.ENetRole"), GetOwnerRole()), *GetName());
}
const AActor* UParametricMovementComponent::GetVLogOwner() const
{
	return GetOwner();
}

void UParametricMovementComponent::VisualLog(const FParametricInputCmd* Input, const FParametricSyncState* Sync, const FParametricAuxState* Aux, const FVisualLoggingParameters& SystemParameters) const
{
	FTransform Transform;
	ParametricMotion.MapTimeToTransform(Sync->Position, Transform);
	FVisualLoggingHelpers::VisualLogActor(GetOwner(), Transform, SystemParameters);
	//DrawParams.LogText = FString::Printf(TEXT("[%d] %s. Position: %.4f. Location: %s. Rotation: %s"), Parameters.Frame, *LexToString(Parameters.Context), Position, *Transform.GetLocation().ToString(), *Transform.GetRotation().Rotator().ToString());
}

void UParametricMovementComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bEnableDependentSimulation && NetSimModel.IsValid() && NetSimModel->GetParentSimulation() == nullptr)
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
				if (INetworkedSimulationModel* ParentNetSim = NetComponent->GetNetworkSimulation())
				{
					NetSimModel->SetParentSimulation(ParentNetSim);
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

void UParametricMovementComponent::ProduceInput(const FNetworkSimTime DeltaTimeSeconds, FParametricInputCmd& Cmd)
{
	Cmd.PlayRate = PendingPlayRate;
	PendingPlayRate.Reset();
}

// -------------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------------

void FSimpleParametricMotion::MapTimeToTransform(const float InPosition, FTransform& OutTransform) const
{
	const FVector Delta = ParametricDelta * InPosition;

	OutTransform = CachedStartingTransform;
	OutTransform.AddToTranslation(Delta);
}

void FSimpleParametricMotion::AdvanceParametricTime(const float InPosition, const float InPlayRate, float &OutPosition, float& OutPlayRate, const float DeltaTimeSeconds) const
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
