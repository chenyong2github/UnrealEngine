// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassExecutionContext.h"
#include "MassArchetypeData.h"


void FMassExecutionContext::FlushDeferred(UMassEntitySubsystem& EntitySystem) const
{
	if (bFlushDeferredCommands && DeferredCommandBuffer)
	{
		EntitySystem.FlushCommands(DeferredCommandBuffer);
	}
}

void FMassExecutionContext::ClearExecutionData()
{
	FragmentViews.Reset();
	CurrentArchetypesTagBitSet.Reset();
	ChunkSerialModificationNumber = -1;
	ConstSubsystemsBitSet.Reset();
	MutableSubsystemsBitSet.Reset();
}

void FMassExecutionContext::SetSubsystemRequirements(const FMassExternalSubystemBitSet& RequiredConstSubsystems, const FMassExternalSubystemBitSet& RequiredMutableSubsystems)
{
	ConstSubsystemsBitSet = RequiredConstSubsystems;
	MutableSubsystemsBitSet = RequiredMutableSubsystems;
}

void FMassExecutionContext::SetEntityCollection(const FMassArchetypeEntityCollection& InEntityCollection)
{
	check(EntityCollection.IsEmpty());
	EntityCollection = InEntityCollection;
}

void FMassExecutionContext::SetEntityCollection(FMassArchetypeEntityCollection&& InEntityCollection)
{
	check(EntityCollection.IsEmpty());
	EntityCollection = MoveTemp(InEntityCollection);
}

void FMassExecutionContext::SetRequirements(TConstArrayView<FMassFragmentRequirementDescription> InRequirements, 
	TConstArrayView<FMassFragmentRequirementDescription> InChunkRequirements, 
	TConstArrayView<FMassFragmentRequirementDescription> InConstSharedRequirements, 
	TConstArrayView<FMassFragmentRequirementDescription> InSharedRequirements)
{ 
	FragmentViews.Reset();
	for (const FMassFragmentRequirementDescription& Requirement : InRequirements)
	{
		if (Requirement.RequiresBinding())
		{
			FragmentViews.Emplace(Requirement);
		}
	}

	ChunkFragmentViews.Reset();
	for (const FMassFragmentRequirementDescription& Requirement : InChunkRequirements)
	{
		if (Requirement.RequiresBinding())
		{
			ChunkFragmentViews.Emplace(Requirement);
		}
	}

	ConstSharedFragmentViews.Reset();
	for (const FMassFragmentRequirementDescription& Requirement : InConstSharedRequirements)
	{
		if (Requirement.RequiresBinding())
		{
			ConstSharedFragmentViews.Emplace(Requirement);
		}
	}

	SharedFragmentViews.Reset();
	for (const FMassFragmentRequirementDescription& Requirement : InSharedRequirements)
	{
		if (Requirement.RequiresBinding())
		{
			SharedFragmentViews.Emplace(Requirement);
		}
	}
}

