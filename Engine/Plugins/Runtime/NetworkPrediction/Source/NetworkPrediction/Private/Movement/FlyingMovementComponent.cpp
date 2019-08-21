// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "Movement/FlyingMovement.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/CharacterMovementComponent.h" // Temp
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "Engine/LocalPlayer.h"
#include "Misc/CoreDelegates.h"
#include "UObject/CoreNet.h"
#include "Engine/NetDriver.h"
#include "Engine/NetConnection.h"
#include "UObject/UObjectIterator.h"
#include "Components/CapsuleComponent.h"
#include "VisualLogger/VisualLogger.h"
#include "Math/Color.h"
#include "DrawDebugHelpers.h"
#include "Debug/DebugDrawService.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Debug/ReporterGraph.h"
#include "NetworkSimulationModelDebugger.h"

DEFINE_LOG_CATEGORY_STATIC(LogFlyingMovement, Log, All);


namespace FlyingMovementCVars
{

static float PenetrationPullbackDistance = 0.125f;
static FAutoConsoleVariableRef CVarPenetrationPullbackDistance(TEXT("fp.PenetrationPullbackDistance"),
	PenetrationPullbackDistance,
	TEXT("Pull out from penetration of an object by this extra distance.\n")
	TEXT("Distance added to penetration fix-ups."),
	ECVF_Default);

static float PenetrationOverlapCheckInflation = 0.100f;
static FAutoConsoleVariableRef CVarPenetrationOverlapCheckInflation(TEXT("motion.PenetrationOverlapCheckInflation"),
	PenetrationOverlapCheckInflation,
	TEXT("Inflation added to object when checking if a location is free of blocking collision.\n")
	TEXT("Distance added to inflation in penetration overlap check."),
	ECVF_Default);

static int32 RequestMispredict = 0;
static FAutoConsoleVariableRef CVarRequestMispredict(TEXT("fp.RequestMispredict"),
	RequestMispredict, TEXT("Causes a misprediction by inserting random value into stream on authority side"), ECVF_Default);
}

// ----------------------------------------------------------------------------------------------------------
//	UFlyingMovementComponent setup/init
// ----------------------------------------------------------------------------------------------------------

UFlyingMovementComponent::UFlyingMovementComponent()
{

}

// ----------------------------------------------------------------------------------------------------------
//	Core Network Prediction functions
// ----------------------------------------------------------------------------------------------------------

IReplicationProxy* UFlyingMovementComponent::InstantiateNetworkSimulation()
{
	NetworkSim.Reset(new FlyingMovement::FMovementSystem());

#if NETSIM_MODEL_DEBUG
	FNetworkSimulationModelDebuggerManager::Get().RegisterNetworkSimulationModel(NetworkSim.Get(), (IFlyingMovementDriver*)this, GetOwner(), TEXT("FlyingPawnMovement"));
#endif
	return NetworkSim.Get();
}

void UFlyingMovementComponent::InitializeForNetworkRole(ENetRole Role)
{	
	check(NetworkSim);
	NetworkSim->InitializeForNetworkRole(Role, GetSimulationInitParameters(Role));
}

void UFlyingMovementComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	// TEMP! Disable existing CMC if it is activate. Just makes A/B testing eaiser for now.
	if (AActor* Owner = GetOwner())
	{
		if (UCharacterMovementComponent* OldComp = Owner->FindComponentByClass<UCharacterMovementComponent>())
		{
			if (OldComp->IsActive())
			{
				OldComp->Deactivate();
			}
		}

		Owner->bReplicateMovement = false;
	}



	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const ENetRole OwnerRole = GetOwnerRole();

	// -------------------------------------
	// Tick the network sim
	// -------------------------------------
	if (NetworkSim)
	{
		PreTickSimulation(DeltaTime); // Fixme

		// Check if we should trip a mispredict. (Note how its not possible to do this inside the Update function!)
		if (OwnerRole == ROLE_Authority && FlyingMovementCVars::RequestMispredict)
		{
			FlyingMovement::FMovementSystem::ForceMispredict = true;
			FlyingMovementCVars::RequestMispredict = 0;
		}

		FlyingMovement::FMovementSystem::FTickParameters Parameters(DeltaTime, GetOwner());;

		// Tick the core network sim, this will consume input and generate new sync state
		NetworkSim->Tick((IFlyingMovementDriver*)this, Parameters);

		// Client->Server communication
		if (NetworkSim->ShouldSendServerRPC(OwnerRole, DeltaTime))
		{
			// Temp hack to make sure the ServerRPC doesn't get suppressed from bandwidth limiting
			// (system hasn't been optimized and not mature enough yet to handle gaps in input stream)
			FScopedBandwidthLimitBypass BandwidthBypass(GetOwner());

			FServerReplicationRPCParameter ProxyParameter(ReplicationProxy_ServerRPC);
			ServerRecieveClientInput(ProxyParameter);
		}
	}
}

// ----------------------------------------------------------------------------------------------------------
//	Movement System Driver
//
//	NOTE: Most of the Movement Driver is not ideal! We are at the mercy of the UpdateComponent since it is the
//	the object that owns its collision data and its MoveComponent function. Ideally we would have everything within
//	the movement simulation code and it do its own collision queries. But instead we have to come back to the Driver/Component
//	layer to do this kind of stuff.
//
// ----------------------------------------------------------------------------------------------------------


void UFlyingMovementComponent::InitSyncState(FlyingMovement::FMoveState& OutSyncState) const
{
	OutSyncState.Location = UpdatedComponent->GetComponentLocation();
	OutSyncState.Rotation = UpdatedComponent->GetComponentQuat().Rotator();	
}

void UFlyingMovementComponent::PreSimSync(const FlyingMovement::FMoveState& SyncState)
{
	// Does checking equality make any sense here? This is unfortunate
	if (UpdatedComponent->GetComponentLocation().Equals(SyncState.Location) == false || UpdatedComponent->GetComponentQuat().Rotator().Equals(SyncState.Rotation, FlyingMovement::ROTATOR_TOLERANCE) == false)
	{
		FTransform Transform(SyncState.Rotation.Quaternion(), SyncState.Location, UpdatedComponent->GetComponentTransform().GetScale3D() );
		UpdatedComponent->SetWorldTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);

		UpdatedComponent->ComponentVelocity = SyncState.Velocity;
	}
}

void UFlyingMovementComponent::ProduceInput(const FlyingMovement::TSimTime& SimFrameTime, FlyingMovement::FInputCmd& Cmd)
{
	// This isn't ideal. It probably makes sense for the component to do all the input binding rather.
	ProduceInputDelegate.ExecuteIfBound(SimFrameTime, Cmd);
}

void UFlyingMovementComponent::FinalizeFrame(const FlyingMovement::FMoveState& SyncState)
{
	PreSimSync(SyncState);
}

FString UFlyingMovementComponent::GetDebugName() const
{
	return FString::Printf(TEXT("FlyingMovement. %s. %s"), *UEnum::GetValueAsString(TEXT("Engine.ENetRole"), GetOwnerRole()), *GetName());
}