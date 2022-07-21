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
	ChunkFragmentViews.Reset();
	ConstSharedFragmentViews.Reset();
	SharedFragmentViews.Reset();
	CurrentArchetypesTagBitSet.Reset();
	ChunkSerialModificationNumber = -1;
}

bool FMassExecutionContext::CacheSubsystem(const UWorld* World, const uint32 SystemIndex)
{
	if (UNLIKELY(Subsystems.IsValidIndex(SystemIndex) == false))
	{
		Subsystems.AddZeroed(Subsystems.Num() - SystemIndex + 1);
	}

	if (Subsystems[SystemIndex])
	{
		return true;
	}

	const UClass* SubsystemClass = FMassExternalSubsystemBitSet::GetTypeAtIndex(SystemIndex);
	if (SubsystemClass)
	{
		const TSubclassOf<UWorldSubsystem> WorldSubsystemClass(const_cast<UClass*>(SubsystemClass));
		if (*WorldSubsystemClass)
		{
			USubsystem* SystemInstance = FMassExternalSubsystemTraits::GetInstance<UWorldSubsystem>(World, WorldSubsystemClass);
			Subsystems[SystemIndex] = SystemInstance;
			return SystemInstance != nullptr;
		}
		else
		{
			// not a UWorldSubsystem. There's no way to generically fetch an instance of one so we assume it's there
			return true;
		}
	}
	return false;
}

bool FMassExecutionContext::CacheSubsystemRequirements(const UWorld* World, const FMassSubsystemRequirements& SubsystemRequirements)
{
	if (SubsystemRequirements.IsEmpty())
	{
		return true;
	}

	for (FMassExternalSubsystemBitSet::FIndexIterator It = SubsystemRequirements.GetRequiredConstSubsystems().GetIndexIterator(); It; ++It)
	{
		if (CacheSubsystem(World, *It) == false)
		{
			return false;
		}
	}

	for (FMassExternalSubsystemBitSet::FIndexIterator It = SubsystemRequirements.GetRequiredMutableSubsystems().GetIndexIterator(); It; ++It)
	{
		if (CacheSubsystem(World, *It) == false)
		{
			return false;
		}
	}

	ConstSubsystemsBitSet = SubsystemRequirements.GetRequiredConstSubsystems();
	MutableSubsystemsBitSet = SubsystemRequirements.GetRequiredMutableSubsystems();
	return true;
}

void FMassExecutionContext::SetSubsystemRequirements(const FMassSubsystemRequirements& SubsystemRequirements)
{
	ConstSubsystemsBitSet = SubsystemRequirements.GetRequiredConstSubsystems();
	MutableSubsystemsBitSet = SubsystemRequirements.GetRequiredMutableSubsystems();
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

void FMassExecutionContext::SetFragmentRequirements(const FMassFragmentRequirements& FragmentRequirements)
{
	FragmentViews.Reset();
	for (const FMassFragmentRequirementDescription& Requirement : FragmentRequirements.GetFragmentRequirements())
	{
		if (Requirement.RequiresBinding())
		{
			FragmentViews.Emplace(Requirement);
		}
	}

	ChunkFragmentViews.Reset();
	for (const FMassFragmentRequirementDescription& Requirement : FragmentRequirements.GetChunkFragmentRequirements())
	{
		if (Requirement.RequiresBinding())
		{
			ChunkFragmentViews.Emplace(Requirement);
		}
	}

	ConstSharedFragmentViews.Reset();
	for (const FMassFragmentRequirementDescription& Requirement : FragmentRequirements.GetConstSharedFragmentRequirements())
	{
		if (Requirement.RequiresBinding())
		{
			ConstSharedFragmentViews.Emplace(Requirement);
		}
	}

	SharedFragmentViews.Reset();
	for (const FMassFragmentRequirementDescription& Requirement : FragmentRequirements.GetSharedFragmentRequirements())
	{
		if (Requirement.RequiresBinding())
		{
			SharedFragmentViews.Emplace(Requirement);
		}
	}
}
