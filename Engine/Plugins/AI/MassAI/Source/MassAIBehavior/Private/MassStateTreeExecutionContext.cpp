// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassStateTreeExecutionContext.h"
#include "MassStateTreeTypes.h"
#include "MassEntitySubsystem.h"
#include "MassSignalSubsystem.h"
#include "Engine/World.h"

FMassStateTreeExecutionContext::FMassStateTreeExecutionContext(UMassEntitySubsystem& InEntitySubsystem,
                                                               FLWComponentSystemExecutionContext& InContext):
	EntitySubsystem(&InEntitySubsystem), EntitySubsystemExecutionContext(&InContext)
{
	World = InEntitySubsystem.GetWorld();
	SignalSubsystem = UWorld::GetSubsystem<UMassSignalSubsystem>(World);
}

void FMassStateTreeExecutionContext::BeginGatedTransition(const FStateTreeExecutionState& Exec)
{
	if (SignalSubsystem != nullptr && Entity.IsSet())
	{
		// Tick again after the games time has passed to see if the condition still holds true.
		SignalSubsystem->DelaySignalEntity(UE::Mass::Signals::DelayedTransitionWakeup, Entity, Exec.GatedTransitionTime + KINDA_SMALL_NUMBER);
	}
}
