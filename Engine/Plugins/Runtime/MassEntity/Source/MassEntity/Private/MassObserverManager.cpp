// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassObserverManager.h"
#include "MassEntitySubsystem.h"
#include "MassExecutor.h"
#include "MassProcessingTypes.h"
#include "MassObserverRegistry.h"

namespace UE::Mass::ObserverManager::Private
{
// a helper function to reduce code duplication in FMassObserverManager::Initialize
template<typename TBitSet>
void SetUpObservers(UMassEntitySubsystem& EntitySubsystem, const TMap<const UScriptStruct*, FMassProcessorClassCollection>& RegisteredObserverTypes, TBitSet& ObservedBitSet, FMassItemObserversMap& Observers)
{
	ObservedBitSet.Reset();

	for (auto It : RegisteredObserverTypes)
	{
		if (It.Value.ClassCollection.Num() == 0)
		{
			continue;
		}

		ObservedBitSet.Add(*It.Key);
		FMassRuntimePipeline& Pipeline = (*Observers).FindOrAdd(It.Key);

		for (const TSubclassOf<UMassProcessor>& ProcessorClass : It.Value.ClassCollection)
		{
			Pipeline.AppendProcessor(ProcessorClass, EntitySubsystem);
		}
		Pipeline.Initialize(EntitySubsystem);
	}
};

} // UE::Mass::ObserverManager::Private

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

	using UE::Mass::ObserverManager::Private::SetUpObservers;
	SetUpObservers(EntitySubsystem, Registry.FragmentInitializersMap, ObservedFragments[(uint8)FMassObservedOperation::Add], FragmentObservers[(uint8)FMassObservedOperation::Add]);
	SetUpObservers(EntitySubsystem, Registry.FragmentDeinitializersMap, ObservedFragments[(uint8)FMassObservedOperation::Remove], FragmentObservers[(uint8)FMassObservedOperation::Remove]);
	SetUpObservers(EntitySubsystem, Registry.TagAddedObserversMap, ObservedTags[(uint8)FMassObservedOperation::Add], TagObservers[(uint8)FMassObservedOperation::Add]);
	SetUpObservers(EntitySubsystem, Registry.TagRemovedObserversMap, ObservedTags[(uint8)FMassObservedOperation::Remove], TagObservers[(uint8)FMassObservedOperation::Remove]);
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


	const FMassFragmentBitSet Overlap = ObservedFragments[(uint8)FMassObservedOperation::Add].GetOverlap(ArchetypeComposition.Fragments);

	if (Overlap.IsEmpty() == false)
	{
		TArray<const UScriptStruct*> OverlapTypes;
		Overlap.ExportTypes(OverlapTypes);

		HandleFragmentsImpl(ProcessingContext, ChunkCollection, MakeArrayView(OverlapTypes), FragmentObservers[(uint8)FMassObservedOperation::Add]);
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
	
	return OnCompositionChanged(ChunkCollection, ArchetypeComposition, FMassObservedOperation::Remove, &ProcessingContext);
}

bool FMassObserverManager::OnCompositionChanged(const FMassArchetypeSubChunks& ChunkCollection, const FMassArchetypeCompositionDescriptor& CompositionDelta, const FMassObservedOperation Operation, FMassProcessingContext* InProcessingContext)
{
	const FMassFragmentBitSet FragmentOverlap = ObservedFragments[(uint8)Operation].GetOverlap(CompositionDelta.Fragments);
	const bool bHasFragmentsOverlap = !FragmentOverlap.IsEmpty();
	const FMassTagBitSet TagOverlap = ObservedTags[(uint8)Operation].GetOverlap(CompositionDelta.Tags);
	const bool bHasTagsOverlap = !TagOverlap.IsEmpty();

	if (bHasFragmentsOverlap || bHasTagsOverlap)
	{
		FMassProcessingContext LocalContext(EntitySubsystem, /*DeltaSeconds=*/0.f);
		FMassProcessingContext* ProcessingContext = InProcessingContext ? InProcessingContext : &LocalContext;
		TArray<const UScriptStruct*> ObservedTypesOverlap;

		if (bHasFragmentsOverlap)
		{
			FragmentOverlap.ExportTypes(ObservedTypesOverlap);

			HandleFragmentsImpl(*ProcessingContext, ChunkCollection, ObservedTypesOverlap, FragmentObservers[(uint8)Operation]);
		}

		if (bHasTagsOverlap)
		{
			ObservedTypesOverlap.Reset();
			TagOverlap.ExportTypes(ObservedTypesOverlap);

			HandleFragmentsImpl(*ProcessingContext, ChunkCollection, ObservedTypesOverlap, TagObservers[(uint8)Operation]);
		}
	}

	return bHasFragmentsOverlap || bHasTagsOverlap;
}

bool FMassObserverManager::OnCompositionChanged(const FMassEntityHandle Entity, const FMassArchetypeCompositionDescriptor& CompositionDelta, const FMassObservedOperation Operation)
{
	const FMassFragmentBitSet FragmentOverlap = ObservedFragments[(uint8)Operation].GetOverlap(CompositionDelta.Fragments);
	const bool bHasFragmentsOverlap = !FragmentOverlap.IsEmpty();
	const FMassTagBitSet TagOverlap = ObservedTags[(uint8)Operation].GetOverlap(CompositionDelta.Tags);
	const bool bHasTagsOverlap = !TagOverlap.IsEmpty();

	if (bHasFragmentsOverlap || bHasTagsOverlap)
	{
		TArray<const UScriptStruct*> ObservedTypesOverlap;
		FMassProcessingContext ProcessingContext(EntitySubsystem, /*DeltaSeconds=*/0.f);
		const FMassArchetypeHandle ArchetypeHandle = EntitySubsystem.GetArchetypeForEntity(Entity);

		if (bHasFragmentsOverlap)
		{
			FragmentOverlap.ExportTypes(ObservedTypesOverlap);

			HandleFragmentsImpl(ProcessingContext, FMassArchetypeSubChunks(ArchetypeHandle, MakeArrayView(&Entity, 1)
				, FMassArchetypeSubChunks::NoDuplicates), ObservedTypesOverlap, FragmentObservers[(uint8)Operation]);
		}

		if (bHasTagsOverlap)
		{
			ObservedTypesOverlap.Reset();
			TagOverlap.ExportTypes(ObservedTypesOverlap);

			HandleFragmentsImpl(ProcessingContext, FMassArchetypeSubChunks(ArchetypeHandle, MakeArrayView(&Entity, 1)
				, FMassArchetypeSubChunks::NoDuplicates), ObservedTypesOverlap, TagObservers[(uint8)Operation]);
		}
	}

	return bHasFragmentsOverlap || bHasTagsOverlap;
}

void FMassObserverManager::OnSingleItemOperation(const UScriptStruct& ItemType, const FMassArchetypeSubChunks& ChunkCollection, const FMassObservedOperation Operation)
{
	check(ItemType.IsChildOf(FMassFragment::StaticStruct()) || ItemType.IsChildOf(FMassTag::StaticStruct()));

	if (ItemType.IsChildOf(FMassFragment::StaticStruct()))
	{
		if (ObservedFragments[(uint8)Operation].Contains(ItemType))
		{
			HandleSingleItemImpl(ItemType, ChunkCollection, FragmentObservers[(uint8)Operation]);
		}
	}
	else if (ObservedTags[(uint8)Operation].Contains(ItemType))
	{
		HandleSingleItemImpl(ItemType, ChunkCollection, TagObservers[(uint8)Operation]);
	}
}

void FMassObserverManager::HandleFragmentsImpl(FMassProcessingContext& ProcessingContext, const FMassArchetypeSubChunks& ChunkCollection
	, TArrayView<const UScriptStruct*> ObservedTypes
	/*, const FMassFragmentBitSet& FragmentsBitSet*/, FMassItemObserversMap& HandlersContainer)
{	
	check(ObservedTypes.Num() > 0);

	for (const UScriptStruct* Type : ObservedTypes)
	{		
		ProcessingContext.AuxData.InitializeAs(Type);
		FMassRuntimePipeline& Pipeline = (*HandlersContainer).FindChecked(Type);

		UE::Mass::Executor::RunProcessorsView(Pipeline.Processors, ProcessingContext, &ChunkCollection);
	}
}

void FMassObserverManager::HandleSingleItemImpl(const UScriptStruct& FragmentType, const FMassArchetypeSubChunks& ChunkCollection, FMassItemObserversMap& HandlersContainer)
{
	FMassProcessingContext ProcessingContext(EntitySubsystem, /*DeltaSeconds=*/0.f);
	ProcessingContext.AuxData.InitializeAs(&FragmentType);
	FMassRuntimePipeline& Pipeline = (*HandlersContainer).FindChecked(&FragmentType);

	UE::Mass::Executor::RunProcessorsView(Pipeline.Processors, ProcessingContext, &ChunkCollection);
}

void FMassObserverManager::AddObserverInstance(const UScriptStruct& ItemType, const FMassObservedOperation Operation, UMassProcessor& ObserverProcessor)
{
	checkSlow(ItemType.IsChildOf(FMassFragment::StaticStruct()) || ItemType.IsChildOf(FMassTag::StaticStruct()));

	FMassRuntimePipeline* Pipeline = nullptr;

	if (ItemType.IsChildOf(FMassFragment::StaticStruct()))
	{
		Pipeline = &(*FragmentObservers[(uint8)Operation]).FindOrAdd(&ItemType);
		ObservedFragments[(uint8)Operation].Add(ItemType);
	}
	else
	{
		Pipeline = &(*TagObservers[(uint8)Operation]).FindOrAdd(&ItemType);
		ObservedTags[(uint8)Operation].Add(ItemType);
	}
	Pipeline->AppendProcessor(ObserverProcessor);

	// calling initialize to ensure the given processor is related to the same EntitySubsystem
	ObserverProcessor.Initialize(EntitySubsystem);
}
