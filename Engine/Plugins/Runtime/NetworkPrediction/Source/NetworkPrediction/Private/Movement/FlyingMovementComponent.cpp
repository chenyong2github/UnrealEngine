// Copyright Epic Games, Inc. All Rights Reserved.

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

DEFINE_LOG_CATEGORY_STATIC(LogFlyingMovement, Log, All);


namespace FlyingMovementCVars
{
static float MaxSpeed = 1200.f;
static FAutoConsoleVariableRef CVarMaxSpeed(TEXT("motion.MaxSpeed"),
	MaxSpeed,
	TEXT("Temp value for testing changes to max speed."),
	ECVF_Default);

static int32 RequestMispredict = 0;
static FAutoConsoleVariableRef CVarRequestMispredict(TEXT("fp.RequestMispredict"),
	RequestMispredict, TEXT("Causes a misprediction by inserting random value into stream on authority side"), ECVF_Default);
}

float UFlyingMovementComponent::GetDefaultMaxSpeed() { return FlyingMovementCVars::MaxSpeed; }

// ----------------------------------------------------------------------------------------------------------
//	UFlyingMovementComponent setup/init
// ----------------------------------------------------------------------------------------------------------

UFlyingMovementComponent::UFlyingMovementComponent()
{

}

// ----------------------------------------------------------------------------------------------------------
//	Core Network Prediction functions
// ----------------------------------------------------------------------------------------------------------

INetworkedSimulationModel* UFlyingMovementComponent::InstantiateNetworkedSimulation()
{
	// The Simulation
	FFlyingMovementSyncState InitialSyncState;
	FFlyingMovementAuxState InitialAuxState;

	InitFlyingMovementSimulation(new FFlyingMovementSimulation(), InitialSyncState, InitialAuxState);

	// The Model
	auto* NewModel = new TNetworkedSimulationModel<FFlyingMovementNetSimModelDef>(this->MovementSimulation, this, InitialSyncState, InitialAuxState);
	InitFlyingMovementNetSimModel(NewModel);
	return NewModel;
}

void UFlyingMovementComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	// TEMP! Disable existing CMC if it is activate. Just makes A/B testing easier for now.
	if (AActor* Owner = GetOwner())
	{
		if (UCharacterMovementComponent* OldComp = Owner->FindComponentByClass<UCharacterMovementComponent>())
		{
			if (OldComp->IsActive())
			{
				OldComp->Deactivate();
			}
		}

		Owner->SetReplicatingMovement(false);
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const ENetRole OwnerRole = GetOwnerRole();

	// Check if we should trip a mispredict. (Note how its not possible to do this inside the Update function!)
	if (OwnerRole == ROLE_Authority && FlyingMovementCVars::RequestMispredict)
	{
		FFlyingMovementSimulation::ForceMispredict = true;
		FlyingMovementCVars::RequestMispredict = 0;
	}

	// Temp
	/*
	if (OwnerRole == ROLE_Authority)
	{
		if (MovementAuxState->Get()->MaxSpeed != FlyingMovementCVars::MaxSpeed)
		{
			MovementAuxState->Modify([](FFlyingMovementAuxState& Aux)
			{
				Aux.MaxSpeed = FlyingMovementCVars::MaxSpeed;
			});
		}
	}
	*/
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

void UFlyingMovementComponent::ProduceInput(const FNetworkSimTime SimTime, FFlyingMovementInputCmd& Cmd)
{
	// This isn't ideal. It probably makes sense for the component to do all the input binding rather.
	ProduceInputDelegate.ExecuteIfBound(SimTime, Cmd);
}

void UFlyingMovementComponent::FinalizeFrame(const FFlyingMovementSyncState& SyncState, const FFlyingMovementAuxState& AuxState)
{
	// Does checking equality make any sense here? This is unfortunate
	if (UpdatedComponent->GetComponentLocation().Equals(SyncState.Location) == false || UpdatedComponent->GetComponentQuat().Rotator().Equals(SyncState.Rotation, FFlyingMovementNetSimModelDef::ROTATOR_TOLERANCE) == false)
	{
		FTransform Transform(SyncState.Rotation.Quaternion(), SyncState.Location, UpdatedComponent->GetComponentTransform().GetScale3D() );
		UpdatedComponent->SetWorldTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);

		UpdatedComponent->ComponentVelocity = SyncState.Velocity;
	}
}

FString UFlyingMovementComponent::GetDebugName() const
{
	return FString::Printf(TEXT("FlyingMovement. %s. %s"), *UEnum::GetValueAsString(TEXT("Engine.ENetRole"), GetOwnerRole()), *GetPathName());
}

const AActor* UFlyingMovementComponent::GetVLogOwner() const
{
	return GetOwner();
}

void UFlyingMovementComponent::VisualLog(const FFlyingMovementInputCmd* Input, const FFlyingMovementSyncState* Sync, const FFlyingMovementAuxState* Aux, const FVisualLoggingParameters& SystemParameters) const
{
	FTransform Transform(Sync->Rotation, Sync->Location);
	FVisualLoggingHelpers::VisualLogActor(GetOwner(), Transform, SystemParameters);	
}