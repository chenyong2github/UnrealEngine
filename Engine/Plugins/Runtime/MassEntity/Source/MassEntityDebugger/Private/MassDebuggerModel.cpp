// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassDebuggerModel.h"
#include "MassProcessor.h"
#include "MassEntitySubsystem.h"
#include "MassEntityQuery.h"
#include "Engine/Engine.h"
#include "MassDebugger.h"
#include "MassDebuggerSettings.h"
#include "UObject/UObjectIterator.h"
#include "Containers/UnrealString.h"

#define LOCTEXT_NAMESPACE "SMassDebugger"

namespace UE::Mass::Debugger::Private
{
	template<typename TBitSet>
	int32 BitSetDistance(const TBitSet& A, const TBitSet& B)
	{
		return (A - B).CountStoredTypes() + (B - A).CountStoredTypes();
	}

	float CalcArchetypeBitDistance(FMassDebuggerArchetypeData& A, FMassDebuggerArchetypeData& B)
	{
		int32 TotalLength = A.Composition.CountStoredTypes() +  B.Composition.CountStoredTypes();

		check(TotalLength > 0);

		return float(BitSetDistance(A.Composition.Fragments, B.Composition.Fragments)
			+ BitSetDistance(A.Composition.Tags, B.Composition.Tags)
			+ BitSetDistance(A.Composition.ChunkFragments, B.Composition.ChunkFragments)
			+ BitSetDistance(A.Composition.SharedFragments, B.Composition.SharedFragments)) 
			/ TotalLength;
	}

	void MakeDisplayName(const FString& InName, FString& OutDisplayName)
	{
		OutDisplayName = InName;
		if (GET_MASSDEBUGGER_CONFIG_VALUE(bStripMassPrefix) == false)
		{
			return;
		}
		
		OutDisplayName.RemoveFromStart(TEXT("Mass"), ESearchCase::CaseSensitive);
	}

	uint32 CalcProcessorHash(const UMassProcessor& Processor)
	{
		return PointerHash(&Processor);
	}
} // namespace UE::Mass::Debugger::Private

//----------------------------------------------------------------------//
// FMassDebuggerEnvironment
//----------------------------------------------------------------------//
FString FMassDebuggerEnvironment::GetDisplayName() const
{
	return World.IsValid() ? World->GetPathName() : TEXT("Invalid");
}

const UMassEntitySubsystem* FMassDebuggerEnvironment::GetEntitySubsystem() const
{
	if (World.IsValid())
	{
		return World->GetSubsystem<UMassEntitySubsystem>();
	}
	return nullptr;
}

//----------------------------------------------------------------------//
// FMassDebuggerQueryData
//----------------------------------------------------------------------//
FMassDebuggerQueryData::FMassDebuggerQueryData(const FMassEntityQuery& Query, const FText& InLabel)
	: Label(InLabel)
{
#if WITH_MASSENTITY_DEBUG
	FMassDebugger::GetQueryExecutionRequirements(Query, ExecutionRequirements);
#endif // WITH_MASSENTITY_DEBUG
}

int32 FMassDebuggerQueryData::GetTotalBitsUsedCount() 
{
	return ExecutionRequirements.GetTotalBitsUsedCount();
}

bool FMassDebuggerQueryData::IsEmpty() const
{
	return ExecutionRequirements.IsEmpty();
}

//----------------------------------------------------------------------//
// FMassDebuggerProcessorData
//----------------------------------------------------------------------//
FMassDebuggerProcessorData::FMassDebuggerProcessorData(const UMassProcessor& InProcessor)
{
	SetProcessor(InProcessor);
#if WITH_MASSENTITY_DEBUG
	TConstArrayView<FMassEntityQuery*> ProcessorQueries = FMassDebugger::GetProcessorQueries(InProcessor);

	if (ensureMsgf(ProcessorQueries.Num() >= 1, TEXT("We expect at least one query to be there - every processor should have their ProcessorRequirements registered in OwnedQueries")))
	{
		ProcessorRequirements = MakeShareable(new FMassDebuggerQueryData(*ProcessorQueries[0], LOCTEXT("MassProcessorRequirementsLabel", "Processor Requirements")));

		Queries.Reserve(ProcessorQueries.Num() - 1);
		for (int i = 1; i < ProcessorQueries.Num(); ++i)
		{
			const FMassEntityQuery* Query = ProcessorQueries[i];
			check(Query);
			Queries.Add(MakeShareable(new FMassDebuggerQueryData(*Query, LOCTEXT("MassEntityQueryLabel", "Query"))));
		}
	}
#endif // WITH_MASSENTITY_DEBUG
}

FMassDebuggerProcessorData::FMassDebuggerProcessorData(const UMassEntitySubsystem& EntitySubsystem, UMassProcessor& InProcessor, const TMap<FMassArchetypeHandle, TSharedPtr<FMassDebuggerArchetypeData>>& InTransientArchetypesMap)
{
	SetProcessor(InProcessor);
#if WITH_MASSENTITY_DEBUG
	TConstArrayView<FMassEntityQuery*> ProcessorQueries = FMassDebugger::GetUpToDateProcessorQueries(EntitySubsystem, InProcessor);
	if (ensureMsgf(ProcessorQueries.Num() >= 1, TEXT("We expect at least one query to be there - every processor should have their ProcessorRequirements registered in OwnedQueries")))
	{
		ProcessorRequirements = MakeShareable(new FMassDebuggerQueryData(*ProcessorQueries[0], LOCTEXT("MassProcessorRequirementsLabel", "Processor Requirements")));

		Queries.Reserve(ProcessorQueries.Num() - 1);
		for (int i = 1; i < ProcessorQueries.Num(); ++i)
		{
			const FMassEntityQuery* Query = ProcessorQueries[i];
			check(Query);
			Queries.Add(MakeShareable(new FMassDebuggerQueryData(*Query, LOCTEXT("MassEntityQueryLabel", "Query"))));

			for (const FMassArchetypeHandle& ArchetypeHandle : Query->GetArchetypes())
			{
				ValidArchetypes.Add(InTransientArchetypesMap.FindChecked(ArchetypeHandle));
			}
		}
	}
#endif // WITH_MASSENTITY_DEBUG
}

void FMassDebuggerProcessorData::SetProcessor(const UMassProcessor& InProcessor)
{
	Name = InProcessor.GetProcessorName();
	UE::Mass::Debugger::Private::MakeDisplayName(Name, Label);

	ProcessorHash = UE::Mass::Debugger::Private::CalcProcessorHash(InProcessor);
}

//----------------------------------------------------------------------//
// FMassDebuggerArchetypeData
//----------------------------------------------------------------------//
FMassDebuggerArchetypeData::FMassDebuggerArchetypeData(const FMassArchetypeHandle& ArchetypeHandle)
{
#if WITH_MASSENTITY_DEBUG
	Composition = FMassDebugger::GetArchetypeComposition(ArchetypeHandle);
	SharedFragments = FMassDebugger::GetArchetypeSharedFragmentValues(ArchetypeHandle);

	// @todo should ensure we're using same hashing as the EntitySubsystem here
	FamilyHash = Composition.CalculateHash();
	FullHash = HashCombine(FamilyHash, GetTypeHash(SharedFragments));

	BytesToHexLower(reinterpret_cast<const uint8*>(&FullHash), sizeof(FullHash), Label);

	FMassDebugger::GetArchetypeEntityStats(ArchetypeHandle, EntitiesCount, EntitiesCountPerChunk, ChunksCount);
#endif // WITH_MASSENTITY_DEBUG
}

int32 FMassDebuggerArchetypeData::GetTotalBitsUsedCount() const
{
	return Composition.CountStoredTypes();
}

//----------------------------------------------------------------------//
// FMassDebuggerProcessingGraphNode
//----------------------------------------------------------------------//
FMassDebuggerProcessingGraphNode::FMassDebuggerProcessingGraphNode(const TSharedPtr<FMassDebuggerProcessorData>& InProcessorData, const UMassCompositeProcessor::FDependencyNode& InProcessorNode)
	: ProcessorData(InProcessorData)
{
	if (InProcessorNode.Processor == nullptr)
	{
		return;
	}

	WaitForNodes = InProcessorNode.Dependencies;
}

FText FMassDebuggerProcessingGraphNode::GetLabel() const
{
	if (ProcessorData.IsValid())
	{
		return FText::FromString(ProcessorData->Label);
	}

	return LOCTEXT("InvalidProcessor", "Invalid");
}

//----------------------------------------------------------------------//
// FMassDebuggerProcessingGraph
//----------------------------------------------------------------------//
FMassDebuggerProcessingGraph::FMassDebuggerProcessingGraph(const FMassDebuggerModel& DebuggerModel, UMassCompositeProcessor& InGraphOwner)
{
	Label = InGraphOwner.GetProcessorName();
#if WITH_MASSENTITY_DEBUG
	TConstArrayView<UMassCompositeProcessor::FDependencyNode> ProcessingGraph = FMassDebugger::GetProcessingGraph(InGraphOwner);

	GraphNodes.Reserve(ProcessingGraph.Num());
	for (const UMassCompositeProcessor::FDependencyNode& Node : ProcessingGraph)
	{
		check(Node.Processor);
		const TSharedPtr<FMassDebuggerProcessorData>& ProcessorData = DebuggerModel.GetProcessorDataChecked(*Node.Processor);
		check(ProcessorData.IsValid());
		GraphNodes.Add(FMassDebuggerProcessingGraphNode(ProcessorData, Node));
	}
#endif // WITH_MASSENTITY_DEBUG
}

//----------------------------------------------------------------------//
// FMassDebuggerModel
//----------------------------------------------------------------------//
void FMassDebuggerModel::SetEnvironment(const TSharedPtr<FMassDebuggerEnvironment>& Item)
{
	if (Item)
	{
		Environment = Item;
		EnvironmentDisplayName = Item->GetDisplayName();
	}
	else
	{
		Environment = nullptr;
		EnvironmentDisplayName.Reset();
	}

	RefreshAll();
}

void FMassDebuggerModel::RefreshAll()
{
	TMap<FMassArchetypeHandle, TSharedPtr<FMassDebuggerArchetypeData>> TransientArchetypesMap;

	CacheArchetypesData(TransientArchetypesMap);
	CacheProcessorsData(TransientArchetypesMap);
	CacheProcessingGraphs();

	OnRefreshDelegate.Broadcast();
}

void FMassDebuggerModel::SelectProcessor(TSharedPtr<FMassDebuggerProcessorData>& Processor)
{
	SelectProcessors(MakeArrayView(&Processor, 1), ESelectInfo::Direct);
}

void FMassDebuggerModel::SelectProcessors(TArrayView<TSharedPtr<FMassDebuggerProcessorData>> Processors, ESelectInfo::Type SelectInfo)
{
	SelectionMode = EMassDebuggerSelectionMode::Processor;

	ResetSelectedProcessors();
	ResetSelectedArchetypes();

	SelectedProcessors = Processors;

	for (TSharedPtr<FMassDebuggerProcessorData>& ProcessorData : SelectedProcessors)
	{
		check(ProcessorData.IsValid());
		ProcessorData->Selection = EMassDebuggerProcessorSelection::Selected;

		for (TSharedPtr<FMassDebuggerArchetypeData>& ArchetypeData : ProcessorData->ValidArchetypes)
		{
			SelectedArchetypes.AddUnique(ArchetypeData);
			ArchetypeData->bIsSelected = true;
		}
	}

	OnProcessorsSelectedDelegate.Broadcast(SelectedProcessors, SelectInfo);
}

void FMassDebuggerModel::ClearProcessorSelection()
{
	SelectionMode = EMassDebuggerSelectionMode::None;

	ResetSelectedProcessors();

	OnProcessorsSelectedDelegate.Broadcast(SelectedProcessors, ESelectInfo::Direct);
}

void FMassDebuggerModel::SelectArchetypes(TArray<TSharedPtr<FMassDebuggerArchetypeData>> InSelectedArchetypes, ESelectInfo::Type SelectInfo)
{
	ResetSelectedProcessors();
	ResetSelectedArchetypes();

	SelectionMode = EMassDebuggerSelectionMode::Archetype;

	SelectedArchetypes = InSelectedArchetypes;

	for (TSharedPtr<FMassDebuggerProcessorData>& ProcessorData : CachedProcessors)
	{
		check(ProcessorData.IsValid());
		for (const TSharedPtr<FMassDebuggerArchetypeData>& ArchetypeData : InSelectedArchetypes)
		{
			if (ProcessorData->ValidArchetypes.Find(ArchetypeData) != INDEX_NONE)
			{
				ProcessorData->Selection = EMassDebuggerProcessorSelection::Selected;
				SelectedProcessors.Add(ProcessorData);
				break;
			}
		}
	}

	OnArchetypesSelectedDelegate.Broadcast(SelectedArchetypes, SelectInfo);
}

void FMassDebuggerModel::ClearArchetypeSelection()
{
	SelectionMode = EMassDebuggerSelectionMode::None;

	ResetSelectedArchetypes();
	OnArchetypesSelectedDelegate.Broadcast(SelectedArchetypes, ESelectInfo::Direct);
}

void FMassDebuggerModel::CacheProcessorsData(const TMap<FMassArchetypeHandle, TSharedPtr<FMassDebuggerArchetypeData>>& InTransientArchetypesMap)
{
	CachedProcessors.Reset();

	UWorld* World = Environment ? Environment->World.Get() : nullptr;
	const UMassEntitySubsystem* EntitySubsystem = Environment ? Environment->GetEntitySubsystem() : nullptr;

	if (EntitySubsystem)
	{
		check(World);
		for (FThreadSafeObjectIterator It(UMassProcessor::StaticClass()); It; ++It)
		{
			UMassProcessor* Processor = Cast<UMassProcessor>(*It);
			if (Processor && (Processor->HasAnyFlags(RF_ClassDefaultObject) == false)
				&& Cast<UMassCompositeProcessor>(Processor) == nullptr
				&& Processor->GetWorld() == World)
			{
				CachedProcessors.Add(MakeShareable(new FMassDebuggerProcessorData(*EntitySubsystem, *Processor, InTransientArchetypesMap)));
			}
		}
	}
	else
	{
		for (FThreadSafeObjectIterator It(UMassProcessor::StaticClass()); It; ++It)
		{
			UMassProcessor* Processor = Cast<UMassProcessor>(*It);
			if (Processor && (Processor->HasAnyFlags(RF_ClassDefaultObject) == false)
				&& Cast<UMassCompositeProcessor>(Processor) == nullptr 
				&& (World == nullptr || Processor->GetWorld() == World))
			{
				CachedProcessors.Add(MakeShareable(new FMassDebuggerProcessorData(*Processor)));
			}
		}
	}

	CachedProcessors.Sort([](const TSharedPtr<FMassDebuggerProcessorData>& A, const TSharedPtr<FMassDebuggerProcessorData>& B)
	{
		return A->Label < B->Label;
	});
}

void FMassDebuggerModel::CacheProcessingGraphs()
{
	CachedProcessingGraphs.Reset();

	UWorld* World = Environment ? Environment->World.Get() : nullptr;
	for (FThreadSafeObjectIterator It(UMassProcessor::StaticClass()); It; ++It)
	{
		UMassCompositeProcessor* Processor = Cast<UMassCompositeProcessor>(*It);
		if (Processor && (Processor->HasAnyFlags(RF_ClassDefaultObject) == false)
			&& (World == nullptr || Processor->GetWorld() == World))
		{
			CachedProcessingGraphs.Add(MakeShareable(new FMassDebuggerProcessingGraph(*this, *Processor)));
		}
	}	
}

void FMassDebuggerModel::CacheArchetypesData(TMap<FMassArchetypeHandle, TSharedPtr<FMassDebuggerArchetypeData>>& OutTransientArchetypesMap)
{
	CachedArchetypes.Reset();

	if (Environment)
	{
		if (const UMassEntitySubsystem* EntitySubsystem = Environment->GetEntitySubsystem())
		{
			StoreArchetypes(*EntitySubsystem, OutTransientArchetypesMap);
		}
	}
}

float FMassDebuggerModel::MinDistanceToSelectedArchetypes(const TSharedPtr<FMassDebuggerArchetypeData>& InArchetypeData) const
{
	float MinDistance = MAX_flt;
	for (const TSharedPtr<FMassDebuggerArchetypeData>& SelectedArchetype : SelectedArchetypes)
	{
		MinDistance = FMath::Min(MinDistance, ArchetypeDistances[SelectedArchetype->Index][InArchetypeData->Index]);
	}
	return MinDistance;
}

void FMassDebuggerModel::StoreArchetypes(const UMassEntitySubsystem& EntitySubsystem, TMap<FMassArchetypeHandle, TSharedPtr<FMassDebuggerArchetypeData>>& OutTransientArchetypesMap)
{
#if WITH_MASSENTITY_DEBUG
	TArray<FMassArchetypeHandle> ArchetypeHandles = FMassDebugger::GetAllArchetypes(EntitySubsystem);

	CachedArchetypes.Reset(ArchetypeHandles.Num());

	int32 MaxBitsUsed = 0;

	// @todo build an archetype handle map
	for (FMassArchetypeHandle& ArchetypeHandle : ArchetypeHandles)
	{
		FMassDebuggerArchetypeData* ArchetypeDataPtr = new FMassDebuggerArchetypeData(ArchetypeHandle);
		ArchetypeDataPtr->Index = CachedArchetypes.Add(MakeShareable(ArchetypeDataPtr));
		OutTransientArchetypesMap.Add(ArchetypeHandle, CachedArchetypes.Last());

		MaxBitsUsed = FMath::Max(MaxBitsUsed, ArchetypeDataPtr->GetTotalBitsUsedCount());
	}
#endif // WITH_MASSENTITY_DEBUG

	// calculate distances
	ArchetypeDistances.Reset();
	ArchetypeDistances.AddDefaulted(CachedArchetypes.Num());
	for (int i = 0; i < CachedArchetypes.Num(); ++i)
	{
		ArchetypeDistances[i].AddDefaulted(CachedArchetypes.Num());
	}

	for (int i = 0; i < CachedArchetypes.Num(); ++i)
	{
		for (int k = i + 1; k < CachedArchetypes.Num(); ++k)
		{
			const float Distance = UE::Mass::Debugger::Private::CalcArchetypeBitDistance(*CachedArchetypes[i].Get(), *CachedArchetypes[k].Get());
			ArchetypeDistances[i][k] = Distance;
			ArchetypeDistances[k][i] = Distance;
		}
	}
}

FText FMassDebuggerModel::GetDisplayName() const
{
	if (!Environment)
	{
		return LOCTEXT("PickEnvironment", "Pick Environment");
	}
	else if (IsStale())
	{
		return FText::FromString(FString::Printf(TEXT("(%s) %s")
				, *(LOCTEXT("StaleEnvironmentPrefix", "Stale").ToString())
				, *EnvironmentDisplayName));
	}

	return FText::FromString(Environment->GetDisplayName());
}

void FMassDebuggerModel::MarkAsStale()
{
	if (Environment)
	{
		Environment->World = nullptr;
	}
}

bool FMassDebuggerModel::IsStale() const 
{ 
	return Environment.IsValid() && (Environment->IsWorldValid() == false); 
}

const TSharedPtr<FMassDebuggerProcessorData>& FMassDebuggerModel::GetProcessorDataChecked(const UMassProcessor& Processor) const
{
	check(CachedProcessors.Num());

	const uint32 ProcessorHash = UE::Mass::Debugger::Private::CalcProcessorHash(Processor);

	const TSharedPtr<FMassDebuggerProcessorData>* DataFound = CachedProcessors.FindByPredicate([ProcessorHash](const TSharedPtr<FMassDebuggerProcessorData>& Element)
		{
			return Element->ProcessorHash == ProcessorHash;
		});

	check(DataFound);
	return *DataFound;
}

void FMassDebuggerModel::ResetSelectedArchetypes()
{
	for (TSharedPtr<FMassDebuggerArchetypeData>& ArchetypeData : SelectedArchetypes)
	{
		ArchetypeData->bIsSelected = false;
	}
	SelectedArchetypes.Reset();
}

void FMassDebuggerModel::ResetSelectedProcessors()
{
	// using CachedProcessors instead of SelectedProcessors to be on the safe side
	for (TSharedPtr<FMassDebuggerProcessorData>& ProcessorData : CachedProcessors)
	{
		check(ProcessorData.IsValid());
		ProcessorData->Selection = EMassDebuggerProcessorSelection::None;
	}
	SelectedProcessors.Reset();
}

#undef LOCTEXT_NAMESPACE
