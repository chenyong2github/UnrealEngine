// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassObserverManager.h"
#include "MassEntitySubsystem.h"
#include "MassExecutor.h"
#include "MassProcessingTypes.h"
#include "MassObserverRegistry.h"

//----------------------------------------------------------------------//
// FMassObserverManager
//----------------------------------------------------------------------//
FMassObserverManager::FMassObserverManager()
	: EntitySubsystem(*GetMutableDefault<UMassEntitySubsystem>())
{

}

FMassObserverManager::FMassObserverManager(UMassEntitySubsystem& Owner)
	: EntitySubsystem(Owner)
{

}

void FMassObserverManager::Initialize()
{
	// instantiate initializers
	const UMassObserverRegistry& Registry = UMassObserverRegistry::Get();

	ObservedAddFragments.Reset();
	ObservedRemoveFragments.Reset();
	OnFragmentAddedObservers.Reset();
	OnFragmentRemovedObservers.Reset();

	for (auto It : Registry.FragmentInitializersMap)
	{
		if (It.Value.ClassCollection.Num() == 0)
		{
			continue;
		}

		ObservedAddFragments.Add(*It.Key);
		FMassRuntimePipeline& Pipeline = OnFragmentAddedObservers.FindOrAdd(It.Key);

		for (const TSubclassOf<UMassProcessor>& ProcessorClass : It.Value.ClassCollection)
		{
			Pipeline.AppendProcessor(ProcessorClass, EntitySubsystem);
		}
		Pipeline.Initialize(EntitySubsystem);
	}

	for (auto It : Registry.FragmentDeinitializersMap)
	{
		if (It.Value.ClassCollection.Num() == 0)
		{
			continue;
		}

		ObservedRemoveFragments.Add(*It.Key);
		FMassRuntimePipeline& Pipeline = OnFragmentRemovedObservers.FindOrAdd(It.Key);

		for (const TSubclassOf<UMassProcessor>& ProcessorClass : It.Value.ClassCollection)
		{
			Pipeline.AppendProcessor(ProcessorClass, EntitySubsystem);
		}
		Pipeline.Initialize(EntitySubsystem);
	}
}

bool FMassObserverManager::OnPostEntitiesCreated(const FMassArchetypeSubChunks& ChunkCollection)
{
	FMassProcessingContext ProcessingContext(EntitySubsystem, /*DeltaSeconds=*/0.f);
	// requesting not to flush commands since handling creation of new entities can result in multiple collections of
	// processors being executed and flushing commands between these runs would ruin ChunkCollection since entities could
	// get their composition changed and get moved to new archetypes
	ProcessingContext.bFlushCommandBuffer = false;
	ProcessingContext.CommandBuffer = MakeShareable(new FMassCommandBuffer());

	if (OnPostEntitiesCreated(ProcessingContext, ChunkCollection))
	{
		ProcessingContext.CommandBuffer->ReplayBufferAgainstSystem(&EntitySubsystem);
		return true;
	}
	return false;
}

bool FMassObserverManager::OnPostEntitiesCreated(FMassProcessingContext& ProcessingContext, const FMassArchetypeSubChunks& ChunkCollection)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("OnPostEntitiesCreated")

	check(ProcessingContext.EntitySubsystem);
	const FMassArchetypeCompositionDescriptor& ArchetypeComposition = ProcessingContext.EntitySubsystem->GetArchetypeComposition(ChunkCollection.GetArchetype());
	const FMassFragmentBitSet Overlap = ObservedAddFragments.GetOverlap(ArchetypeComposition.Fragments);

	if (Overlap.IsEmpty() == false)
	{
		HandleFragmentsImpl(ProcessingContext, ChunkCollection, Overlap, OnFragmentAddedObservers);
		return true;
	}

	return false;
}

bool FMassObserverManager::OnPreEntitiesDestroyed(const FMassArchetypeSubChunks& ChunkCollection)
{
	FMassProcessingContext ProcessingContext(EntitySubsystem, /*DeltaSeconds=*/0.f);
	ProcessingContext.bFlushCommandBuffer = false;
	ProcessingContext.CommandBuffer = MakeShareable(new FMassCommandBuffer());

	if (OnPreEntitiesDestroyed(ProcessingContext, ChunkCollection))
	{
		ProcessingContext.CommandBuffer->ReplayBufferAgainstSystem(&EntitySubsystem);
		return true;
	}
	return false;
}

bool FMassObserverManager::OnPreEntitiesDestroyed(FMassProcessingContext& ProcessingContext, const FMassArchetypeSubChunks& ChunkCollection)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("OnPreEntitiesDestroyed")

	check(ProcessingContext.EntitySubsystem);
	const FMassArchetypeCompositionDescriptor& ArchetypeComposition = ProcessingContext.EntitySubsystem->GetArchetypeComposition(ChunkCollection.GetArchetype());
	const FMassFragmentBitSet Overlap = ObservedRemoveFragments.GetOverlap(ArchetypeComposition.Fragments);

	if (Overlap.IsEmpty() == false)
	{
		HandleFragmentsImpl(ProcessingContext, ChunkCollection, Overlap, OnFragmentRemovedObservers);
		return true;
	}

	return false;
}

bool FMassObserverManager::OnPostCompositionAdded(const FMassEntityHandle Entity, const FMassArchetypeCompositionDescriptor& Composition)
{
	const FMassFragmentBitSet Overlap = ObservedAddFragments.GetOverlap(Composition.Fragments);
	if (Overlap.IsEmpty() == false)
	{
		FMassProcessingContext ProcessingContext(EntitySubsystem, /*DeltaSeconds=*/0.f);
		const FMassArchetypeHandle ArchetypeHandle = EntitySubsystem.GetArchetypeForEntity(Entity);
		HandleFragmentsImpl(ProcessingContext, FMassArchetypeSubChunks(ArchetypeHandle, MakeArrayView(&Entity, 1)
			, FMassArchetypeSubChunks::NoDuplicates), Overlap, OnFragmentAddedObservers);
		return true;
	}

	return false;
}

bool FMassObserverManager::OnPreCompositionRemoved(const FMassEntityHandle Entity, const FMassArchetypeCompositionDescriptor& Composition)
{
	const FMassFragmentBitSet Overlap = ObservedRemoveFragments.GetOverlap(Composition.Fragments);
	if (Overlap.IsEmpty() == false)
	{
		FMassProcessingContext ProcessingContext(EntitySubsystem, /*DeltaSeconds=*/0.f);
		const FMassArchetypeHandle ArchetypeHandle = EntitySubsystem.GetArchetypeForEntity(Entity);
		HandleFragmentsImpl(ProcessingContext, FMassArchetypeSubChunks(ArchetypeHandle, MakeArrayView(&Entity, 1)
			, FMassArchetypeSubChunks::NoDuplicates), Overlap, OnFragmentRemovedObservers);
		return true;
	}

	return false;
}

void FMassObserverManager::HandleFragmentsImpl(FMassProcessingContext& ProcessingContext, const FMassArchetypeSubChunks& ChunkCollection, const FMassFragmentBitSet& FragmentsBitSet, TMap<const UScriptStruct*, FMassRuntimePipeline>& HandlersContainer)
{
	TArray<const UScriptStruct*> Fragments;
	FragmentsBitSet.ExportTypes(Fragments);
	
	for (const UScriptStruct* FragmentType : Fragments)
	{		
		ProcessingContext.AuxData.InitializeAs(FragmentType);
		FMassRuntimePipeline& Pipeline = HandlersContainer.FindChecked(FragmentType);

		UE::Mass::Executor::RunProcessorsView(Pipeline.Processors, ProcessingContext, &ChunkCollection);
	}
}

void FMassObserverManager::HandleSingleFragmentImpl(const UScriptStruct& FragmentType, const FMassArchetypeSubChunks& ChunkCollection, const FMassFragmentBitSet& FragmentFilterBitSet, TMap<const UScriptStruct*, FMassRuntimePipeline>& HandlersContainer)
{
	if (FragmentFilterBitSet.Contains(FragmentType))
	{
		FMassProcessingContext ProcessingContext(EntitySubsystem, /*DeltaSeconds=*/0.f);
		ProcessingContext.AuxData.InitializeAs(&FragmentType);
		FMassRuntimePipeline& Pipeline = HandlersContainer.FindChecked(&FragmentType	);

		UE::Mass::Executor::RunProcessorsView(Pipeline.Processors, ProcessingContext, &ChunkCollection);
	}
}
