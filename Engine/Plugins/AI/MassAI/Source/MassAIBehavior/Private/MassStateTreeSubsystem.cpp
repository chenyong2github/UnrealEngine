// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassStateTreeSubsystem.h"
#include "StateTree.h"
#include "Engine/Engine.h"


void UMassStateTreeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	
}

FMassStateTreeHandle UMassStateTreeSubsystem::RegisterStateTreeAsset(UStateTree* StateTree)
{
	// Return already registered asset if found.
	int32 Index = RegisteredStateTrees.IndexOfByPredicate([StateTree](UStateTree* ExistingStateTree) -> bool { return ExistingStateTree == StateTree; });
	if (Index != INDEX_NONE)
	{
		return FMassStateTreeHandle::Make(uint16(Index));
	}
	// Add new, check that it fits the StateTree handle.
	Index = RegisteredStateTrees.Add(StateTree);
	check(Index < int32(MAX_uint16));
	return FMassStateTreeHandle::Make(uint16(Index));
}

