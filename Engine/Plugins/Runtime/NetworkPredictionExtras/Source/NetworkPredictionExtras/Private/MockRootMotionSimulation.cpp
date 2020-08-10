// Copyright Epic Games, Inc. All Rights Reserved.

#include "MockRootMotionSimulation.h"
#include "NetworkPredictionCheck.h"
#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY_STATIC(LogMockRootMotion, Log, All);

// Core tick function for updating root motion.
//	-Handle InputCmd: possibly turn input state into a playing RootMotion source
//	-If RootMotion source is playing, call into RootMotionSourceMap to evaluate (get the delta transform and update the output Sync state)
//	-Use delta transform to actually sweep move our collision through the world

void FMockRootMotionSimulation::SimulationTick(const FNetSimTimeStep& TimeStep, const TNetSimInput<MockRootMotionStateTypes>& Input, const TNetSimOutput<MockRootMotionStateTypes>& Output)
{
	npCheckSlow(this->SourceMap);
	npCheckSlow(UpdatedComponent);
	npCheckSlow(RootMotionComponent);

	FMockRootMotionSyncState LocalSync = *Input.Sync;

	// See if InputCmd wants to play a new RootMotion (only if we aren't currently playing one)
	if (Input.Cmd->PlayCount != Input.Sync->InputPlayCount && LocalSync.RootMotionSourceID == INDEX_NONE)
	{
		// NOTE: This is not a typical gameplay setup. This code essentially allows the client to 
		// start new RootMotionSources anytime it wants to.
		//
		// A real game is going to have higher level logic. Like "InputCmd wants to activate an ability, can we do that?"
		// and if so; "update Sync State to reflect the new animation".
		
		LocalSync.RootMotionSourceID = Input.Cmd->PlaySourceID;
		LocalSync.PlayRate = 1.f; // input initiated root motions are assumed to be 1.f play rate
		LocalSync.PlayPosition = 0.f; // also assumed to start at t=0

		// Question: should we advance the root motion here or not? When you play a montage, do you expect the next render 
		// frame to be @ t=0? Or should we advance it by TimeStep.StepMS? 
		//
		// It seems one frame of no movement would be bad. If we were chaining animations together, we wouldn't want to enforce
		// a system one stationary frame (which will depend on TimeStep.MS!)
	}

	// Always copy the playcount through (otherwise we end up queing new RootMotions while one is playing)
	LocalSync.InputPlayCount = Input.Cmd->PlayCount;

	// Copy input to output
	*Output.Sync = LocalSync;
	
	if (LocalSync.RootMotionSourceID == INDEX_NONE)
	{
		// We aren't playing root motion so there is nothing else to do,
		return;
	}

	// Component has to be put in the right place first
	// FIXME: this has to happen outside of the ::SimulationTick for group reocncniliation to be correct
	// (E.g if another sim ticked before us, our PrimitiveComponent will not be in the right spot (our Sync state transform)
	FTransform StartingTransform(LocalSync.Rotation, LocalSync.Location, UpdatedComponent->GetComponentTransform().GetScale3D());
	UpdatedComponent->SetWorldTransform(StartingTransform, false, nullptr, ETeleportType::TeleportPhysics);

	// Call into root motion source map to actually update the root motino state
	FTransform LocalDeltaTransform = this->SourceMap->StepRootMotion(TimeStep, &LocalSync, Output.Sync);

	// StepRootMotion should return local delta transform, we need to convert to world
	FTransform DeltaWorldTransform;
	{
		// Calculate new actor transform after applying root motion to this component
		// this was lifted from USkeletalMeshComponent::ConvertLocalRootMotionToWorld
		const FTransform ActorToWorld = RootMotionComponent->GetOwner()->GetTransform();

		const FTransform ComponentToActor = ActorToWorld.GetRelativeTransform(RootMotionComponent->GetComponentTransform());
		const FTransform NewComponentToWorld = LocalDeltaTransform * RootMotionComponent->GetComponentTransform();
		const FTransform NewActorTransform = ComponentToActor * NewComponentToWorld;

		const FVector DeltaWorldTranslation = NewActorTransform.GetTranslation() - ActorToWorld.GetTranslation();

		const FQuat NewWorldRotation = RootMotionComponent->GetComponentTransform().GetRotation() * LocalDeltaTransform.GetRotation();
		const FQuat DeltaWorldRotation = NewWorldRotation * RootMotionComponent->GetComponentTransform().GetRotation().Inverse();

		DeltaWorldTransform = FTransform(DeltaWorldRotation, DeltaWorldTranslation);

		/*
		UE_LOG(LogRootMotion, Log,  TEXT("ConvertLocalRootMotionToWorld LocalT: %s, LocalR: %s, WorldT: %s, WorldR: %s."),
			*InTransform.GetTranslation().ToCompactString(), *InTransform.GetRotation().Rotator().ToCompactString(),
			*DeltaWorldTransform.GetTranslation().ToCompactString(), *DeltaWorldTransform.GetRotation().Rotator().ToCompactString());
			*/		
	}

	// ---------------------------------------------------------------------
	// Move the component via collision sweep
	//	-This could be better: to much converting between FTransforms, Rotators, quats, etc.
	//	-Problem of "movement can be blocked but rotation can't". Can be unclear exactly what to do 
	//		(should block in movement cause a block in rotation?)
	// ---------------------------------------------------------------------
	FQuat NewRotation(DeltaWorldTransform.Rotator() + LocalSync.Rotation);

	// Actually do the sweep
	FHitResult HitResult;
	SafeMoveUpdatedComponent(DeltaWorldTransform.GetTranslation(), NewRotation, true, HitResult, ETeleportType::TeleportPhysics);

	// The component was actually moved, so pull transform back out
	FTransform EndTransform = UpdatedComponent->GetComponentTransform(); 
	Output.Sync->Location = EndTransform.GetTranslation();
	Output.Sync->Rotation = EndTransform.GetRotation().Rotator();
}