// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MassArchetypeTypes.h"
#include "MassEntityQuery.h"
#include "MassProcessorDependencySolver.h"
#include "MassProcessingTypes.h"
#include "Types/SlateEnums.h"
#include "MassDebugger.h"


class UMassProcessor;
struct FMassArchetypeHandle;
struct FMassDebuggerModel;
class UMassEntitySubsystem;

enum class EMassDebuggerSelectionMode : uint8
{
	None,
	Processor,
	Archetype,
	// @todo future:
	// Fragment
	MAX
};

enum class EMassDebuggerProcessorSelection : uint8
{
	None,
	Selected,
	MAX
};

enum class EMassDebuggerProcessingGraphNodeSelection : uint8
{
	None,
	WaitFor,
	Block,
	MAX
};


struct FMassDebuggerQueryData
{
	FMassDebuggerQueryData(const FMassEntityQuery& Query);

	FMassExecutionRequirements ExecutionRequirements;

	int32 GetTotalBitsUsedCount();
}; 

struct FMassDebuggerArchetypeData
{
	FMassDebuggerArchetypeData(const FMassArchetypeHandle& ArchetypeHandle);

	FMassArchetypeCompositionDescriptor Composition;
	FMassArchetypeSharedFragmentValues SharedFragments;
	uint32 FamilyHash = 0;
	uint32 FullHash = 0;

	int32 EntitiesCount = 0;
	int32 EntitiesCountPerChunk = 0;
	int32 ChunksCount = 0;

	TArray<TSharedPtr<FMassDebuggerArchetypeData>> Children;

	int32 Index = INDEX_NONE;
	FString Label;

	bool bIsSelected = false;

	int32 GetTotalBitsUsedCount() const;
};

struct FMassDebuggerProcessorData
{
	FMassDebuggerProcessorData(const UMassProcessor& InProcessor);
	FMassDebuggerProcessorData(const UMassEntitySubsystem& EntitySubsystem, UMassProcessor& InProcessor, const TMap<FMassArchetypeHandle, TSharedPtr<FMassDebuggerArchetypeData>>& InTransientArchetypesMap);

private:
	void SetProcessor(const UMassProcessor& InProcessor);

public:
	FString Name;
	FString Label;
	uint32 ProcessorHash = 0; 
	TArray<TSharedPtr<FMassDebuggerQueryData>> Queries;
	TArray<TSharedPtr<FMassDebuggerArchetypeData>> ValidArchetypes;
	
	EMassDebuggerProcessorSelection Selection = EMassDebuggerProcessorSelection::None;
};

struct FMassDebuggerProcessingGraphNode
{
	FMassDebuggerProcessingGraphNode(const TSharedPtr<FMassDebuggerProcessorData>& InProcessorData, const UMassCompositeProcessor::FDependencyNode& InProcessorNode);
	
	FText GetLabel() const;

	TSharedPtr<FMassDebuggerProcessorData> ProcessorData;
	TArray<int32> WaitForNodes;
	TArray<int32> BlockNodes;
	EMassDebuggerProcessingGraphNodeSelection GraphNodeSelection = EMassDebuggerProcessingGraphNodeSelection::None;
};

struct FMassDebuggerProcessingGraph
{
	FMassDebuggerProcessingGraph(const FMassDebuggerModel& DebuggerModel, UMassCompositeProcessor& InGraphOwner);

	FString Label;
	TArray<FMassDebuggerProcessingGraphNode> GraphNodes;
};


struct FMassDebuggerEnvironment
{
	explicit FMassDebuggerEnvironment(UWorld* InWorld)
		: World(InWorld)
	{}

	bool operator==(const FMassDebuggerEnvironment& Other) const { return World == Other.World; }

	FString GetDisplayName() const;
	const UMassEntitySubsystem* GetEntitySubsystem() const;
	bool IsWorldValid() const { return World.IsValid(); }
	
	TWeakObjectPtr<UWorld> World;
};


struct FMassDebuggerModel
{
	DECLARE_MULTICAST_DELEGATE(FOnRefresh);
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnProcessorsSelected, TConstArrayView<TSharedPtr<FMassDebuggerProcessorData>>, ESelectInfo::Type);
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnArchetypesSelected, TConstArrayView<TSharedPtr<FMassDebuggerArchetypeData>>, ESelectInfo::Type);

	void SetEnvironment(const TSharedPtr<FMassDebuggerEnvironment>& Item);

	void RefreshAll();

	void SelectProcessor(TSharedPtr<FMassDebuggerProcessorData>& Processor);
	void SelectProcessors(TArrayView<TSharedPtr<FMassDebuggerProcessorData>> Processors, ESelectInfo::Type SelectInfo);
	void ClearProcessorSelection();

	void SelectArchetypes(TArray<TSharedPtr<FMassDebuggerArchetypeData>> SelectedArchetypes, ESelectInfo::Type SelectInfo);
	void ClearArchetypeSelection();

	bool IsCurrentEnvironment(const FMassDebuggerEnvironment& InEnvironment) const { return Environment && *Environment.Get() == InEnvironment; }

	void CacheArchetypesData(TMap<FMassArchetypeHandle, TSharedPtr<FMassDebuggerArchetypeData>>& OutTransientArchetypesMap); 
	void CacheProcessorsData(const TMap<FMassArchetypeHandle, TSharedPtr<FMassDebuggerArchetypeData>>& InTransientArchetypesMap);
	void CacheProcessingGraphs();

	float MinDistanceToSelectedArchetypes(const TSharedPtr<FMassDebuggerArchetypeData>& InArchetypeData) const;

	FText GetDisplayName() const;

	void MarkAsStale();
	bool IsStale() const;

	const TSharedPtr<FMassDebuggerProcessorData>& GetProcessorDataChecked(const UMassProcessor& Processor) const;

protected:
	void StoreArchetypes(const UMassEntitySubsystem& EntitySubsystem, TMap<FMassArchetypeHandle, TSharedPtr<FMassDebuggerArchetypeData>>& OutTransientArchetypesMap);

	void ResetSelectedArchetypes();
	void ResetSelectedProcessors();

public:
	FOnRefresh OnRefreshDelegate;
	FOnProcessorsSelected OnProcessorsSelectedDelegate;
	FOnArchetypesSelected OnArchetypesSelectedDelegate;

	EMassDebuggerSelectionMode SelectionMode = EMassDebuggerSelectionMode::None;

	TSharedPtr<FMassDebuggerEnvironment> Environment;
	TArray<TSharedPtr<FMassDebuggerProcessorData>> CachedProcessors;
	TArray<TSharedPtr<FMassDebuggerProcessorData>> SelectedProcessors;

	TArray<TSharedPtr<FMassDebuggerArchetypeData>> CachedArchetypes;
	TArray<TSharedPtr<FMassDebuggerArchetypeData>> SelectedArchetypes;

	TArray<TSharedPtr<FMassDebuggerProcessingGraph>> CachedProcessingGraphs;

	TMap<FMassArchetypeHandle, TSharedPtr<FMassDebuggerArchetypeData>> HandleToArchetypeMap;

	TArray<TArray<float>> ArchetypeDistances;

	FString EnvironmentDisplayName;
};

