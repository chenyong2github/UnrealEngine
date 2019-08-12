// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Movement/ParametricMovement.h"
#include "NetworkSimulationModelDebugger.h"

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

static int32 ForceNetUpdate = 0;
static FAutoConsoleVariableRef CVarForceNetUpdate(TEXT("parametricmover.ForceNetUpdate"),
	DrawDebugDefaultLifeTime, TEXT("Toggles calling ForceNetUpdate each tick on Parametric movers. This is only meant for testing/debugging."), ECVF_Default);

}

// -------------------------------------------------------------------------------------------------------------------------------
// ParametricMovement
// -------------------------------------------------------------------------------------------------------------------------------

namespace ParametricMovement
{
	void FMovementSystem::Update(IMovementDriver* Driver, const FInputCmd& InputCmd, const FMoveState& InputState, FMoveState& OutputState, const FAuxState& AuxState)
	{
		IBaseMovementDriver& BaseMovementDriver = Driver->GetBaseMovementDriver();
		const float DeltaSeconds = InputCmd.FrameDeltaTime;

		// Advance parametric time. This won't always be linear: we could loop, rewind/bounce, etc
		const float InputPlayRate = InputCmd.PlayRate.Get(InputState.PlayRate); // Returns InputCmds playrate if set, else returns previous state's playrate		
		Driver->AdvanceParametricTime(InputState.Position, InputPlayRate, OutputState.Position, OutputState.PlayRate, InputCmd.FrameDeltaTime);

		// We have our time that we should be at. We just need to move primitive component to that position.
		// Again, note that we expect this cannot fail. We move like this so that it can push things, but we don't expect failure.
		// (I think it would need to be a different simulation that supports this as a failure case. The tricky thing would be working out
		// the ouput Position in the case where a Move is blocked (E.g, you move 50% towards the desired location)

		FTransform StartingTransform;
		Driver->SetTransformForPosition(InputState.Position, StartingTransform);

		FTransform NewTransform;
		Driver->SetTransformForPosition(OutputState.Position, NewTransform);

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

	void FMoveState::VisualLog(const FVisualLoggingParameters& Parameters, IParametricMovementDriver* Driver, IParametricMovementDriver* LogDriver)
	{
		IBaseMovementDriver& BaseMovementDriver = Driver->GetBaseMovementDriver();
		IBaseMovementDriver& LogMovementDriver = LogDriver->GetBaseMovementDriver();

		FTransform Transform;
		Driver->SetTransformForPosition(Position, Transform);

		IBaseMovementDriver::FDrawDebugParams DrawParams;
		DrawParams.DebugWorld = LogMovementDriver.GetDriverWorld();
		DrawParams.DebugLogOwner = LogMovementDriver.GetVLogOwner();
		DrawParams.Transform = Transform;
		DrawParams.DrawType = (Parameters.Context == EVisualLoggingContext::OtherMispredicted || Parameters.Context == EVisualLoggingContext::OtherPredicted) ? EVisualLoggingDrawType::Crumb : EVisualLoggingDrawType::Full;
		DrawParams.Lifetime = Parameters.Lifetime;
		DrawParams.DrawColor = Parameters.GetDebugColor();
		DrawParams.InWorldText = LexToString(Parameters.Keyframe);
		DrawParams.LogText = FString::Printf(TEXT("[%d] %s. Location: %s. Rotation: %s"), Parameters.Keyframe, *LexToString(Parameters.Context), *Transform.GetLocation().ToString(), *Transform.GetRotation().Rotator().ToString());

		BaseMovementDriver.DrawDebug(DrawParams);
	}
};


// -------------------------------------------------------------------------------------------------------------------------------
// UParametricMovementComponent
// -------------------------------------------------------------------------------------------------------------------------------

UParametricMovementComponent::UParametricMovementComponent()
{

}

IReplicationProxy* UParametricMovementComponent::InstantiateNetworkSimulation()
{
	NetworkSim.Reset(new ParametricMovement::FMovementSystem());

#if NETSIM_MODEL_DEBUG
	FNetworkSimulationModelDebuggerManager::Get().RegisterNetworkSimulationModel(NetworkSim.Get(), (IParametricMovementDriver*)this, GetOwner(), TEXT("ParametricMovement"));
#endif
	return NetworkSim.Get();
}

void UParametricMovementComponent::InitializeForNetworkRole(ENetRole Role)
{	
	check(NetworkSim);
	NetworkSim->InitializeForNetworkRole(Role, true /* fixme IsLocallyControlled() */, GetSimulationInitParameters(Role));
}

FNetworkSimulationModelInitParameters UParametricMovementComponent::GetSimulationInitParameters(ENetRole Role)
{
	// These are reasonable defaults but may not be right for everyone
	FNetworkSimulationModelInitParameters InitParams;
	InitParams.InputBufferSize = 32; //Role != ROLE_SimulatedProxy ? 32 : 32;  // Fixme.. not good
	InitParams.SyncedBufferSize = Role != ROLE_AutonomousProxy ? 2 : 32;
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

float UParametricMovementComponent::GetTransformForTime(const float InTime, FTransform& OutTransform) const
{
	const FVector Delta = ParametricDelta * InTime;
	OutTransform = CachedStartingTransform;
	OutTransform.AddToTranslation(Delta);
	return InTime;
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
	GetTransformForTime(SyncState.Position, NewTransform);

	check(UpdatedComponent);
	UpdatedComponent->SetWorldTransform(NewTransform, false, nullptr, ETeleportType::TeleportPhysics);
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

void UParametricMovementComponent::SetTransformForPosition(const float InPosition, FTransform& OutTransform) const
{
	const FVector Delta = ParametricDelta * InPosition;

	OutTransform = CachedStartingTransform;
	OutTransform.AddToTranslation(Delta);
}

void UParametricMovementComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	TickSimulation(DeltaTime); // fixme
}

void UParametricMovementComponent::Reconcile()
{

}

void UParametricMovementComponent::TickSimulation(float DeltaTimeSeconds)
{
	const ENetRole OwnerRole = GetOwnerRole();

	PreTickSimulation(DeltaTimeSeconds); // Fixme

	if (NetworkSim && OwnerRole != ROLE_None)
	{
		if (OwnerRole == ROLE_Authority)
		{
			if (ParametricMovement::FInputCmd* NextCmd = NetworkSim->GetNextInputForWrite(DeltaTimeSeconds))
			{
				NextCmd->PlayRate = PendingPlayRate;
				PendingPlayRate.Reset();
			}
		}

		ParametricMovement::FMovementSystem::FTickParameters Parameters;
		Parameters.Role = OwnerRole;
		Parameters.LocalDeltaTimeSeconds = DeltaTimeSeconds;

		// Tick the core network sim, this will consume input and generate new sync state
		NetworkSim->Tick((IParametricMovementDriver*)this, Parameters);
	}

	// TEMP
	if (ParametricMoverCVars::ForceNetUpdate)
	{
		GetOwner()->ForceNetUpdate();
	}
}