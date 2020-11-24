// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GeometryFlowNode.h"


namespace UE
{
namespace GeometryFlow
{



// TODO: this needs more work
	// need option to opportunistically cache, ie on multi-use of output, or when we can't re-use anyway
	//   (this is currently what NeverCache does?)

enum class ENodeCachingStrategy
{
	Default = 0,
	AlwaysCache = 1,
	NeverCache = 2
};


//
// TODO: 
// - internal FNode pointers can be unique ptr?
// - parallel evaluation at graph level (pre-pass to collect references?)
//


class GEOMETRYFLOWCORE_API FGraph
{
public:

	struct FHandle
	{
		int32 Identifier;
		bool operator==(const FHandle& OtherHandle) const { return Identifier == OtherHandle.Identifier; }
	};

	template<typename NodeType>
	FHandle AddNodeOfType(
		const FString& Identifier = FString(""),
		ENodeCachingStrategy CachingStrategy = ENodeCachingStrategy::Default )
	{
		FNodeInfo NewNodeInfo;
		NewNodeInfo.Node = MakeShared<NodeType, ESPMode::ThreadSafe>();
		NewNodeInfo.Node->SetIdentifier(Identifier);
		NewNodeInfo.CachingStrategy = CachingStrategy;
		FHandle Handle = { NodeCounter++ };
		AllNodes.Add(Handle.Identifier, NewNodeInfo);
		return Handle;
	}

	EGeometryFlowResult AddConnection(FHandle FromNode, FString FromOutput, FHandle ToNode, FString ToInput);

	EGeometryFlowResult InferConnection(FHandle FromNode, FHandle ToNode);


	template<typename T>
	EGeometryFlowResult EvaluateResult(FHandle Node, FString OutputName, T& Storage, int32 StorageTypeIdentifier)
	{
		TUniquePtr<FEvaluationInfo> EvalInfo = MakeUnique<FEvaluationInfo>();
		return EvaluateResult(Node, OutputName, Storage, StorageTypeIdentifier, EvalInfo);
	}

	template<typename T>
	EGeometryFlowResult EvaluateResult(
		FHandle Node, 
		FString OutputName, 
		T& Storage, 
		int32 StorageTypeIdentifier,
		TUniquePtr<FEvaluationInfo>& EvaluationInfo,
		bool bTryTakeResult)
	{
		int32 OutputType;
		EGeometryFlowResult ValidOutput = GetOutputTypeForNode(Node, OutputName, OutputType);
		if (ValidOutput != EGeometryFlowResult::Ok)
		{
			return ValidOutput;
		}
		if (OutputType != StorageTypeIdentifier)
		{
			return EGeometryFlowResult::UnmatchedTypes;
		}
		if (bTryTakeResult)
		{
			TSafeSharedPtr<IData> Data = ComputeOutputData(Node, OutputName, EvaluationInfo, true);
			Data->GiveTo(Storage, StorageTypeIdentifier);
		}
		else
		{
			TSafeSharedPtr<IData> Data = ComputeOutputData(Node, OutputName, EvaluationInfo, false);
			Data->GetDataCopy(Storage, StorageTypeIdentifier);
		}
		return EGeometryFlowResult::Ok;
	}


	template<typename NodeType>
	EGeometryFlowResult ApplyToNodeOfType(
		FHandle NodeHandle, 
		TFunctionRef<void(NodeType&)> ApplyFunc)
	{
		TSafeSharedPtr<FNode> FoundNode = FindNode(NodeHandle);
		if (FoundNode)
		{
			NodeType* Value = static_cast<NodeType*>(FoundNode.Get());
			ApplyFunc(*Value);
			return EGeometryFlowResult::Ok;
		}
		return EGeometryFlowResult::NodeDoesNotExist;
	}



	void ConfigureCachingStrategy(ENodeCachingStrategy NewStrategy);
	EGeometryFlowResult SetNodeCachingStrategy(FHandle NodeHandle, ENodeCachingStrategy Strategy);

	
protected:
	int32 NodeCounter = 0;

	ENodeCachingStrategy DefaultCachingStrategy = ENodeCachingStrategy::AlwaysCache;

	struct FNodeInfo
	{
		TSafeSharedPtr<FNode> Node;
		ENodeCachingStrategy CachingStrategy = ENodeCachingStrategy::Default;
	};

	TMap<int32, FNodeInfo> AllNodes;

	TSafeSharedPtr<FNode> FindNode(FHandle Handle) const;
	EGeometryFlowResult GetInputTypeForNode(FHandle NodeHandle, FString InputName, int32& Type) const;
	EGeometryFlowResult GetOutputTypeForNode(FHandle NodeHandle, FString OutputName, int32& Type) const;
	ENodeCachingStrategy GetCachingStrategyForNode(FHandle NodeHandle) const;

	struct FConnection
	{
		FHandle FromNode;
		FString FromOutput;

		FHandle ToNode;
		FString ToInput;
	};
	TArray<FConnection> Connections;

	EGeometryFlowResult FindConnectionForInput(FHandle ToNode, FString ToInput, FConnection& ConnectionOut) const;

	int32 CountOutputConnections(FHandle FromNode, const FString& FromOutput) const;

	TSafeSharedPtr<IData> ComputeOutputData(
		FHandle Node, 
		FString OutputName, 
		TUniquePtr<FEvaluationInfo>& EvaluationInfo,
		bool bStealOutputData = false);
};











}	// end namespace GeometryFlow
}	//