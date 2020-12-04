// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendGraph.h"

#include "Algo/AllOf.h"
#include "Algo/AnyOf.h"
#include "Algo/Transform.h"
#include "CoreMinimal.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendDataLayout.h"
#include "MetasoundGraph.h"
#include "MetasoundNodeInterface.h"

namespace Metasound
{
	FFrontendGraph::FFrontendGraph(const FString& InInstanceName)
	:	FGraph(InInstanceName)
	{
	}

	void FFrontendGraph::AddInputNode(int32 InDependencyId, int32 InIndex, const FVertexKey& InVertexKey, TUniquePtr<INode> InNode)
	{
		if (InNode.IsValid())
		{
			// There shouldn't be duplicate IDs. 
			check(!InputNodes.Contains(InIndex));

			// Input nodes need an extra Index value to keep track of their position in the graph's inputs.
			InputNodes.Add(InIndex, InNode.Get());
			AddInputDataDestination(*InNode, InVertexKey);
			AddNode(InDependencyId, MoveTemp(InNode));
		}
	}

	void FFrontendGraph::AddOutputNode(int32 InNodeID, int32 InIndex, const FVertexKey& InVertexKey, TUniquePtr<INode> InNode)
	{
		if (InNode.IsValid())
		{
			// There shouldn't be duplicate IDs. 
			check(!OutputNodes.Contains(InIndex));

			// Output nodes need an extra Index value to keep track of their position in the graph's inputs.
			OutputNodes.Add(InIndex, InNode.Get());
			AddOutputDataSource(*InNode, InVertexKey);
			AddNode(InNodeID, MoveTemp(InNode));
		}
	}

	/** Store a node on this graph. */
	void FFrontendGraph::AddNode(int32 InNodeID, TUniquePtr<INode> InNode)
	{
		if (InNode.IsValid())
		{
			// There shouldn't be duplicate IDs. 
			check(!NodeMap.Contains(InNodeID));

			NodeMap.Add(InNodeID, InNode.Get());
			StoreNode(MoveTemp(InNode));
		}
	}

	const INode* FFrontendGraph::FindNode(int32 InNodeID) const
	{
		INode* const* NodePtr = NodeMap.Find(InNodeID);

		if (nullptr != NodePtr)
		{
			return *NodePtr;
		}

		return nullptr;
	}

	const INode* FFrontendGraph::FindInputNode(int32 InIndex) const
	{
		INode* const* NodePtr = InputNodes.Find(InIndex);

		if (nullptr != NodePtr)
		{
			return *NodePtr;
		}

		return nullptr;
	}

	const INode* FFrontendGraph::FindOutputNode(int32 InIndex) const
	{
		INode* const* NodePtr = OutputNodes.Find(InIndex);

		if (nullptr != NodePtr)
		{
			return *NodePtr;
		}

		return nullptr;
	}

	/** Returns true if all edges, destinations and sources refer to 
	 * nodes stored in this graph. */
	bool FFrontendGraph::OwnsAllReferencedNodes() const
	{
		const TArray<FDataEdge>& AllEdges = GetDataEdges();
		for (const FDataEdge& Edge : AllEdges)
		{
			if (!StoredNodes.Contains(Edge.From.Node))
			{
				return false;
			}

			if (!StoredNodes.Contains(Edge.To.Node))
			{
				return false;
			}
		}

		const FInputDataDestinationCollection& AllInputDestinations = GetInputDataDestinations();
		for (auto& DestTuple : AllInputDestinations)
		{
			if (!StoredNodes.Contains(DestTuple.Value.Node))
			{
				return false;
			}
		}

		const FOutputDataSourceCollection& AllOutputSources = GetOutputDataSources();
		for (auto& SourceTuple : AllOutputSources)
		{
			if (!StoredNodes.Contains(SourceTuple.Value.Node))
			{
				return false;
			}
		}

		return true;
	}


	void FFrontendGraph::StoreNode(TUniquePtr<INode> InNode)
	{
		check(InNode.IsValid());
		StoredNodes.Add(InNode.Get());
		Storage.Add(MoveTemp(InNode));
	}


	TUniquePtr<INode> FFrontendGraphBuilder::CreateInputNode(const FMetasoundNodeDescription& InNode, const FMetasoundInputDescription& InDescription) const
	{
		if (!ensureAlwaysMsgf(Frontend::DoesDataTypeSupportLiteralType(InDescription.TypeName, InDescription.LiteralValue.LiteralType), TEXT("[%s] cannot be constructed with the provided literal type."), *InDescription.TypeName.ToString()))
		{
			return TUniquePtr<INode>(nullptr);
		}

		FDataTypeLiteralParam LiteralParam = Frontend::GetLiteralParamForDataType(InDescription.TypeName, InDescription.LiteralValue);

		FInputNodeConstructorParams InitParams =
		{
			InDescription.Name,
			InDescription.Name,
			MoveTemp(LiteralParam)
		};

		return FMetasoundFrontendRegistryContainer::Get()->ConstructInputNode(InDescription.TypeName, MoveTemp(InitParams));
	}

	TUniquePtr<INode> FFrontendGraphBuilder::CreateOutputNode(const FMetasoundNodeDescription& InNode, const FMetasoundOutputDescription& InDescription) const
	{
		FOutputNodeConstrutorParams InitParams =
		{
			InDescription.Name,
			InDescription.Name,
		};

		return FMetasoundFrontendRegistryContainer::Get()->ConstructOutputNode(InDescription.TypeName, InitParams);
	}

	TUniquePtr<INode> FFrontendGraphBuilder::CreateExternalNode(const FMetasoundNodeDescription& InNode, const FMetasoundClassDescription& InClass) const
	{
		check(InClass.Metadata.NodeType == EMetasoundClassType::External);
		check(InNode.ObjectTypeOfNode == EMetasoundClassType::External);

		// We found a match. now we just need to create the input node.
		const FMetasoundExternalClassLookupInfo& LookupInfo = InClass.ExternalNodeClassLookupInfo;

		FNodeInitData InitData;

		InitData.InstanceName.Append(InNode.Name);
		InitData.InstanceName.AppendChar('_');
		InitData.InstanceName.AppendInt(InNode.UniqueID);
		
		// Copy over our initialization params.
		for (auto& StaticParamTuple : InNode.StaticParameters)
		{
			FDataTypeLiteralParam LiteralParam = Frontend::GetLiteralParam(StaticParamTuple.Value);

			if (LiteralParam.IsValid())
			{
				InitData.ParamMap.Add(StaticParamTuple.Key, MoveTemp(LiteralParam));
			}
		}

		return FMetasoundFrontendRegistryContainer::Get()->ConstructExternalNode(LookupInfo.ExternalNodeClassName, LookupInfo.ExternalNodeClassHash, InitData);
	}

	// TODO: add errors here. Most will be a "PromptIfMissing"...
	void FFrontendGraphBuilder::AddExternalNodesToGraph(const TArray<FMetasoundNodeDescription>& InNodes, const TMap<int32, const FMetasoundClassDescription*>& InClasses, FFrontendGraph& OutGraph) const
	{
		for (const FMetasoundNodeDescription& Node : InNodes)
		{
			if (Node.ObjectTypeOfNode == EMetasoundClassType::External)
			{
				const FMetasoundClassDescription* ClassDesc = InClasses.FindRef(Node.DependencyID);

				if (ensure(nullptr != ClassDesc))
				{
					OutGraph.AddNode(Node.UniqueID, CreateExternalNode(Node, *ClassDesc));
				}
			}
		}
	}

	void FFrontendGraphBuilder::AddEdgesToGraph(const FMetasoundGraphDescription& InGraphDescription, FFrontendGraph& OutGraph) const
	{
		for (const FMetasoundNodeDescription& NodeDescription : InGraphDescription.Nodes)
		{
			const INode* ToNode = OutGraph.FindNode(NodeDescription.UniqueID);

			if (nullptr == ToNode)
			{
				// TODO: log errror. 
				continue;
			}

			for (const FMetasoundNodeConnectionDescription& InputConnection : NodeDescription.InputConnections)
			{
				const bool bIsConnected = InputConnection.NodeID != FMetasoundNodeConnectionDescription::DisconnectedNodeID;
				if (!bIsConnected)
				{
					continue;
				}

				const INode* FromNode = OutGraph.FindNode(InputConnection.NodeID);
				if (nullptr == FromNode)
				{
					// TODO: log error.
					continue;
				}

				OutGraph.AddDataEdge(*FromNode, *InputConnection.OutputName, *ToNode, *InputConnection.InputName);
			}
		}
	}

	void FFrontendGraphBuilder::AddInputDestinationsToGraph(const TArray<FMetasoundInputDescription>& InInputDescriptions, const TArray<FMetasoundNodeDescription>& InNodes, FFrontendGraph& OutGraph) const
	{
		for (int32 InputIndex = 0; InputIndex < InInputDescriptions.Num(); InputIndex++)
		{
			const FMetasoundInputDescription& InputDescription = InInputDescriptions[InputIndex];

			auto IsInputWithSameName = [&](const FMetasoundNodeDescription& InNode) 
			{ 
				return (InNode.Name == InputDescription.Name) && (InNode.ObjectTypeOfNode == EMetasoundClassType::Input);
			};

			const FMetasoundNodeDescription* NodeDescription = InNodes.FindByPredicate(IsInputWithSameName);

			if (nullptr == NodeDescription)
			{
				// TODO: add error.
				continue;
			}

			OutGraph.AddInputNode(NodeDescription->UniqueID, InputIndex, InputDescription.Name, CreateInputNode(*NodeDescription, InputDescription));
		}
	}

	void FFrontendGraphBuilder::AddOutputSourcesToGraph(const TArray<FMetasoundOutputDescription>& InOutputDescriptions, const TArray<FMetasoundNodeDescription>& InNodes, FFrontendGraph& OutGraph) const
	{
		for (int32 OutputIndex = 0; OutputIndex < InOutputDescriptions.Num(); OutputIndex++)
		{
			const FMetasoundOutputDescription& OutputDescription = InOutputDescriptions[OutputIndex];

			auto IsOutputWithSameName = [&](const FMetasoundNodeDescription& InNode) 
			{ 
				return (InNode.Name == OutputDescription.Name) && (InNode.ObjectTypeOfNode == EMetasoundClassType::Output);
			};

			const FMetasoundNodeDescription* NodeDescription = InNodes.FindByPredicate(IsOutputWithSameName);

			if (nullptr == NodeDescription)
			{
				// TODO: add error.
				continue;
			}

			OutGraph.AddOutputNode(NodeDescription->UniqueID, OutputIndex, OutputDescription.Name, CreateOutputNode(*NodeDescription, OutputDescription));
		}
	}

	void FFrontendGraphBuilder::SplitNodesByType(const TArray<FMetasoundNodeDescription>& InNodes, TArray<FMetasoundNodeDescription>& OutExternalNodes, TArray<FMetasoundNodeDescription>& OutInputNodes, TArray<FMetasoundNodeDescription>& OutOutputNodes) const
	{
		for (const FMetasoundNodeDescription& Node : InNodes)
		{
			switch (Node.ObjectTypeOfNode)
			{
				case EMetasoundClassType::External:

					OutExternalNodes.Add(Node);
					break;

				case EMetasoundClassType::Input:

					OutInputNodes.Add(Node);
					break;

				case EMetasoundClassType::Output:

					OutOutputNodes.Add(Node);
					break;

				default:
					checkNoEntry();
			}
		}
	}

	/** Check that all dependencies are C++ class dependencies. */
	bool FFrontendGraphBuilder::IsFlat(const FMetasoundDocument& InDocument) const
	{
		return IsFlat(InDocument.RootClass, InDocument.Dependencies);
	}

	bool FFrontendGraphBuilder::IsFlat(const FMetasoundClassDescription& InRoot, const TArray<FMetasoundClassDescription>& InDependencies) const
	{
		// All dependencies are external dependencies in a flat graph
		auto IsClassExternal = [&](const FMetasoundClassDescription& InDesc) { return InDesc.Metadata.NodeType == EMetasoundClassType::External; };
		const bool bIsEveryDependencyExternal = Algo::AllOf(InDependencies, IsClassExternal);

		if (!bIsEveryDependencyExternal)
		{
			return false;
		}

		// None of the nodes are subgraphs in a flat graph
		auto IsGraphNode = [&](const FMetasoundNodeDescription& InDesc) { return InDesc.ObjectTypeOfNode == EMetasoundClassType::MetasoundGraph; };
		const bool bIsAnyNodeAGraph = Algo::AnyOf(InRoot.Graph.Nodes, IsGraphNode);

		if (bIsAnyNodeAGraph)
		{
			return false;
		}

		// All the dependencies are available in a flat graph.
		TSet<int32> AvailableDependencies;
		Algo::Transform(InDependencies, AvailableDependencies, [](const FMetasoundClassDescription& InDesc) { return InDesc.UniqueID; });

		auto IsDependencyMet = [&](const FMetasoundNodeDescription& InNode) 
		{ 
			// Currently, input/output nodes do not have dependencies. This may change.
			const bool bIsInputOrOutput = (InNode.ObjectTypeOfNode == EMetasoundClassType::Input) || (InNode.ObjectTypeOfNode == EMetasoundClassType::Output);
			const bool bIsExternal = InNode.ObjectTypeOfNode == EMetasoundClassType::External;

			return bIsInputOrOutput || (bIsExternal && AvailableDependencies.Contains(InNode.DependencyID));
		};

		const bool bIsEveryDependencyMet = Algo::AllOf(InRoot.Graph.Nodes, IsDependencyMet);
		return bIsEveryDependencyMet;
	}

	
	/* Metasound document should be inflated by now. */
	TUniquePtr<FFrontendGraph> FFrontendGraphBuilder::CreateGraph(const FMetasoundDocument& InDocument) const
	{
		return CreateGraph(InDocument.RootClass, InDocument.Dependencies);
	}

	TUniquePtr<FFrontendGraph> FFrontendGraphBuilder::CreateGraph(const FMetasoundClassDescription& InRoot, const TArray<FMetasoundClassDescription>& InDependencies) const
	{
		if (!IsFlat(InRoot, InDependencies))
		{
			checkNoEntry();
			return TUniquePtr<FFrontendGraph>(nullptr);
		}

		TUniquePtr<FFrontendGraph> Graph = MakeUnique<FFrontendGraph>(InRoot.Metadata.NodeName);

		TMap<int32, const FMetasoundClassDescription*> NodeMap;

		for (const FMetasoundClassDescription& InDesc : InDependencies)
		{
			NodeMap.Add(InDesc.UniqueID, &InDesc);
		}

		TArray<FMetasoundNodeDescription> ExternalNodes;
		TArray<FMetasoundNodeDescription> InputNodes;
		TArray<FMetasoundNodeDescription> OutputNodes;

		SplitNodesByType(InRoot.Graph.Nodes, ExternalNodes, InputNodes, OutputNodes);

		// TODO: will likely want to bubble up errors here for case where
		// a datatype or node is not registered. 
		AddExternalNodesToGraph(ExternalNodes, NodeMap, *Graph);

		AddInputDestinationsToGraph(InRoot.Inputs, InputNodes, *Graph);

		AddOutputSourcesToGraph(InRoot.Outputs, OutputNodes, *Graph);

		AddEdgesToGraph(InRoot.Graph, *Graph);

		check(Graph->OwnsAllReferencedNodes());

		return MoveTemp(Graph);
	}
}
