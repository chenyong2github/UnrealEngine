// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassStateTreeExecutionContext.h"
#include "MassStateTreeTypes.h"
#include "MassEntitySubsystem.h"
#include "MassSignalSubsystem.h"
#include "Engine/World.h"

FMassStateTreeExecutionContext::FMassStateTreeExecutionContext(FMassEntityManager& InEntityManager,
                                                               UMassSignalSubsystem& InSignalSubsystem,
                                                               FMassExecutionContext& InContext):
	EntityManager(&InEntityManager), SignalSubsystem(&InSignalSubsystem), EntitySubsystemExecutionContext(&InContext)
{
}

void FMassStateTreeExecutionContext::BeginGatedTransition(const FStateTreeExecutionState& Exec)
{
	if (SignalSubsystem != nullptr && Entity.IsSet())
	{
		// Tick again after the games time has passed to see if the condition still holds true.
		SignalSubsystem->DelaySignalEntity(UE::Mass::Signals::DelayedTransitionWakeup, Entity, Exec.GatedTransitionTime + KINDA_SMALL_NUMBER);
	}
}
