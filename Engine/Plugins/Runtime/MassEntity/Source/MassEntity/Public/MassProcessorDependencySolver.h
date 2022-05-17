// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessingTypes.h"
#include "MassEntityTypes.h"
#include "Containers/StaticArray.h"


class UMassProcessor;

enum class EDependencyNodeType : uint8
{
	Invalid,
	Processor,
	GroupStart,
	GroupEnd
};

namespace EMassAccessOperation
{
	constexpr uint32 Read = 0;
	constexpr uint32 Write = 1;
	constexpr uint32 MAX = 2;
};

template<typename T>
struct MASSENTITY_API TMassExecutionAccess
{
	T Read;
	T Write;

	T& operator[](const uint32 OpIndex)
	{
		check(OpIndex <= EMassAccessOperation::MAX);
		return OpIndex == EMassAccessOperation::Read ? Read : Write;
	}

	const T& operator[](const uint32 OpIndex) const
	{
		check(OpIndex <= EMassAccessOperation::MAX);
		return OpIndex == EMassAccessOperation::Read ? Read : Write;
	}
};

struct MASSENTITY_API FMassExecutionRequirements
{
	FMassExecutionRequirements& operator+=(const FMassExecutionRequirements& Other)
	{
		for (int i = 0; i < EMassAccessOperation::MAX; ++i)
		{
			Fragments[i] += Other.Fragments[i];
			ChunkFragments[i] += Other.ChunkFragments[i];
			SharedFragments[i] += Other.SharedFragments[i];
			RequiredSubsystems[i] += Other.RequiredSubsystems[i];
		}
		return *this;
	}

	TMassExecutionAccess<FMassFragmentBitSet> Fragments;
	TMassExecutionAccess<FMassChunkFragmentBitSet> ChunkFragments;
	TMassExecutionAccess<FMassSharedFragmentBitSet> SharedFragments;
	TMassExecutionAccess<FMassExternalSubystemBitSet> RequiredSubsystems;
};

struct MASSENTITY_API FProcessorDependencySolver
{
private:
	struct FNode
	{
		FNode(const FName InName, UMassProcessor* InProcessor) 
			: Name(InName), Processor(InProcessor)
		{}

		FName Name = TEXT("");
		TArray<FNode> SubNodes;
		TMap<FName, int32> Indices;
		UMassProcessor* Processor = nullptr;
		TArray<int32> OriginalDependencies;
		TArray<int32> TransientDependencies;
		TArray<FName> ExecuteBefore;
		TArray<FName> ExecuteAfter;
		FMassExecutionRequirements Requirements;

		int32 FindOrAddGroupNodeIndex(const FString& GroupName);
		int32 FindNodeIndex(FName InNodeName) const;
		bool HasDependencies() const;
	};

	struct FResourceUsage
	{
		FResourceUsage();

		bool CanAccessRequirements(const FMassExecutionRequirements& TestedRequirements) const;
		void SubmitNode(const int32 NodeIndex, FNode& InOutNode);

	private:
		struct FResourceUsers
		{
			TArray<int32> Users;
		};
		
		struct FResourceAccess
		{
			TArray<FResourceUsers> Access;
		};
		
		FMassExecutionRequirements Requirements;
		TMassExecutionAccess<FResourceAccess> FragmentsAccess;
		TMassExecutionAccess<FResourceAccess> ChunkFragmentsAccess;
		TMassExecutionAccess<FResourceAccess> SharedFragmentsAccess;
		TMassExecutionAccess<FResourceAccess> RequiredSubsystemsAccess;

		template<typename TBitSet>
		static void HandleElementType(TMassExecutionAccess<FResourceAccess>& ElementAccess
			, const TMassExecutionAccess<TBitSet>& TestedRequirements, FProcessorDependencySolver::FNode& InOutNode, const int32 NodeIndex);

		template<typename TBitSet>
		static bool CanAccess(const TMassExecutionAccess<TBitSet>& StoredElements, const TMassExecutionAccess<TBitSet>& TestedElements);
	};

public:
	struct FOrderInfo
	{
		FName Name = TEXT("");
		UMassProcessor* Processor = nullptr;
		EDependencyNodeType NodeType = EDependencyNodeType::Invalid;
		TArray<FName> Dependencies;
	};

	FProcessorDependencySolver(TArrayView<UMassProcessor*> InProcessors, const FName Name, const FString& InDependencyGraphFileName = FString());
	void ResolveDependencies(TArray<FOrderInfo>& OutResult, TConstArrayView<const FName> PriorityNodes = TConstArrayView<const FName>());

	static void CreateSubGroupNames(FName InGroupName, TArray<FString>& SubGroupNames);

protected:
	// note that internals are protected rather than private to support unit testing

	/**
	 * Traverses InOutIndicesRemaining in search of the first RootNode's node that has no dependencies left. Once found 
	 * the node's index gets added to OutNodeIndices, removed from dependency lists from all other nodes and the function 
	 * quits.
	 * @return 'true' if a dependency-less node has been found and added to OutNodeIndices; 'false' otherwise.
	 */
	static bool PerformSolverStep(FResourceUsage& ResourceUsage, FNode& RootNode, TArray<int32>& InOutIndicesRemaining, TArray<int32>& OutNodeIndices);
	
	static FString NameViewToString(TConstArrayView<FName> View);

	void AddNode(FName GroupName, UMassProcessor& Processor);
	void BuildDependencies(FNode& RootNode);
	void Solve(FNode& RootNode, TConstArrayView<const FName> PriorityNodes, TArray<FProcessorDependencySolver::FOrderInfo>& OutResult, int LoggingIndent = 0);
	void LogNode(const FNode& RootNode, const FNode* ParentNode = nullptr, int Indent = 0);
	void DumpGraph(FArchive& LogFile) const;
	
	TArrayView<UMassProcessor*> Processors;
	FNode GroupRootNode;
	bool bAnyCyclesDetected = false;
	FString DependencyGraphFileName;

	friend struct FDumpGraphDependencyUtils;
};