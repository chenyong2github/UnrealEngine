// Copyright Epic Games, Inc. All Rights Reserved.

#include "NetworkPredictionPhysics.h"
#include "Physics/Experimental/PhysInterface_Chaos.h"
#include "Chaos/ParticleHandle.h"
#include "Components/PrimitiveComponent.h"

// -------------------------------------------------------------------------------------------------------------------------
//	Interpolation related functions. These currently require calls to the UPrimitiveComponent. 
//	It may be possible one day to make the calls directly to the FPhysicsActorHandle, but for now kinematic bodies are
//	a one way street. The possible physics calls are commented out in the function bodies.
//
//
//	If you are landing here with a nullptr Driver, see notes in FNetworkPredictionDriverBase::SafeCastDriverToPrimitiveComponent
// -------------------------------------------------------------------------------------------------------------------------

void FNetworkPredictionPhysicsState::BeginInterpolation(UPrimitiveComponent* Driver, FPhysicsActorHandle ActorHandle)
{
	npCheckSlow(Driver);
	Driver->SetSimulatePhysics(false);
	
	// FPhysicsCommand::ExecuteWrite(ActorHandle, [&](const FPhysicsActorHandle& Actor)
	// {
	//	FPhysicsInterface::SetIsKinematic_AssumesLocked(Actor, true);
	// });
}

void FNetworkPredictionPhysicsState::EndInterpolation(UPrimitiveComponent* Driver, FPhysicsActorHandle ActorHandle)
{
	npCheckSlow(Driver);
	Driver->SetSimulatePhysics(true);

	// FPhysicsCommand::ExecuteWrite(ActorHandle, [&](const FPhysicsActorHandle& Actor)
	// {
	// FPhysicsInterface::SetIsKinematic_AssumesLocked(Actor, false);
	// });
}

void FNetworkPredictionPhysicsState::FinalizeInterpolatedPhysics(UPrimitiveComponent* Driver, FPhysicsActorHandle ActorHandle, FNetworkPredictionPhysicsState* InterpolatedState)
{
	npCheckSlow(Driver);
	npCheckSlow(InterpolatedState);
	npEnsure(Driver->IsSimulatingPhysics() == false);
	
	npEnsureSlow(InterpolatedState->Location.ContainsNaN() == false);
	npEnsureSlow(InterpolatedState->Rotation.ContainsNaN() == false);

	Driver->SetWorldLocationAndRotation(InterpolatedState->Location, InterpolatedState->Rotation, false);
	
	//	FPhysicsCommand::ExecuteWrite(ActorHandle, [NewTransform](const FPhysicsActorHandle& Actor)
	//	{
	//		FPhysicsInterface::SetKinematicTarget_AssumesLocked(Actor, NewTransform);
	//	});
}