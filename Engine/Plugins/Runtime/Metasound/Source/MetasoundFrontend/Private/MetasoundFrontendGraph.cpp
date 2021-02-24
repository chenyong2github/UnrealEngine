// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendGraph.h"

#include "Algo/AllOf.h"
#include "Algo/AnyOf.h"
#include "Algo/Transform.h"
#include "CoreMinimal.h"
#include "MetasoundFrontend.h"
#include "MetasoundGraph.h"
#include "MetasoundLog.h"
#include "MetasoundNodeInterface.h"

namespace Metasound
{
	FFrontendGraph::FFrontendGraph(const FString& InInstanceName, const FGuid& InInstanceID)
	:	FGraph(InInstanceName, InInstanceID)
	{
	}

	void FFrontendGraph::AddInputNode(FGuid InDependencyId, int32 InIndex, const FVertexKey& InVertexKey, TUniquePtr<INode> InNode)
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

	void FFrontendGraph::AddOutputNode(FGuid InNodeID, int32 InIndex, const FVertexKey& InVertexKey, TUniquePtr<INode> InNode)
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
	void FFrontendGraph::AddNode(FGuid InNodeID, TUniquePtr<INode> InNode)
	{
		if (InNode.IsValid())
		{
			// There shouldn't be duplicate IDs. 
			check(!NodeMap.Contains(InNodeID));

			NodeMap.Add(InNodeID, InNode.Get());
			StoreNode(MoveTemp(InNode));
		}
	}

	const INode* FFrontendGraph::FindNode(FGuid InNodeID) const
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


	TUniquePtr<INode> FFrontendGraphBuilder::CreateInputNode(const FMetasoundFrontendNode& InNode, const FMetasoundFrontendClass& InClass, const FMetasoundFrontendClassInput& InOwningGraphClassInput) const
	{
		const FMetasoundFrontendLiteral* FrontendLiteral = FindInputLiteralForInputNode(InNode, InClass, InOwningGraphClassInput);

		if (nullptr != FrontendLiteral)
		{
			if (ensure(InNode.Interface.Inputs.Num() == 1))
			{
				const FMetasoundFrontendVertex& InputVertex = InNode.Interface.Inputs[0];

				const bool IsLiteralParsableByDataType = Frontend::DoesDataTypeSupportLiteralType(InputVertex.TypeName, FrontendLiteral->GetType());

				if (IsLiteralParsableByDataType)
				{
					FLiteral Literal = FrontendLiteral->ToLiteral(InputVertex.TypeName);

					FInputNodeConstructorParams InitParams =
					{
						InNode.Name,
						InNode.ID,
						InputVertex.Name,
						MoveTemp(Literal)
					};

					// TODO: create input node using external class definition
					return FMetasoundFrontendRegistryContainer::Get()->ConstructInputNode(InputVertex.TypeName, MoveTemp(InitParams));
				}
				else
				{
					UE_LOG(LogMetasound, Error, TEXT("Cannot create input node [NodeID:%s]. [Vertex:%s] cannot be constructed with the provided literal type."), *InNode.ID.ToString(), *InputVertex.Name);
				}
			}
		}
		else
		{
			UE_LOG(LogMetasound, Error, TEXT("Cannot create input node [NodeID:%s]. No default literal set for input node."), *InNode.ID.ToString());
		}

		return TUniquePtr<INode>(nullptr);
	}

	TUniquePtr<INode> FFrontendGraphBuilder::CreateOutputNode(const FMetasoundFrontendNode& InNode, const FMetasoundFrontendClass& InClass) const
	{
		check(InClass.Metadata.Type == EMetasoundFrontendClassType::Output);
		check(InNode.ClassID == InClass.ID);

		if (ensure(InNode.Interface.Outputs.Num() == 1))
		{
			const FMetasoundFrontendVertex& OutputVertex = InNode.Interface.Outputs[0];

			FOutputNodeConstructorParams InitParams =
			{
				InNode.Name,
				InNode.ID,
				OutputVertex.Name,
			};

			// TODO: create output node using external class definition
			return FMetasoundFrontendRegistryContainer::Get()->ConstructOutputNode(OutputVertex.TypeName, InitParams);
		}
		return TUniquePtr<INode>(nullptr);
	}

	TUniquePtr<INode> FFrontendGraphBuilder::CreateExternalNode(const FMetasoundFrontendNode& InNode, const FMetasoundFrontendClass& InClass) const
	{
		check(InClass.Metadata.Type == EMetasoundFrontendClassType::External);
		check(InNode.ClassID == InClass.ID);

		FNodeInitData InitData;

		InitData.InstanceName.Append(InNode.Name);
		InitData.InstanceName.AppendChar('_');
		InitData.InstanceName.Append(InNode.ID.ToString());

		InitData.InstanceID = InNode.ID;


		// Copy over our initialization params.
		/*
		for (auto& StaticParamTuple : InNode.StaticParameters)
		{
			FLiteral LiteralParam = Frontend::GetLiteralParam(StaticParamTuple.Value);

			if (LiteralParam.IsValid())
			{
				InitData.ParamMap.Add(StaticParamTuple.Key, MoveTemp(LiteralParam));
			}
		}
		*/
		
		// TODO: handle check to see if node interface conforms to class interface here. 
		// TODO: check to see if external object supports class interface.

		Metasound::Frontend::FNodeRegistryKey Key = FMetasoundFrontendRegistryContainer::GetRegistryKey(InClass.Metadata);

		return FMetasoundFrontendRegistryContainer::Get()->ConstructExternalNode(Key.NodeClassFullName, Key.NodeHash, InitData);
	}


	const FMetasoundFrontendClassInput* FFrontendGraphBuilder::FindClassInputForInputNode(const FMetasoundFrontendGraphClass& InOwningGraph, const FMetasoundFrontendNode& InInputNode, int32& OutClassInputIndex) const
	{
		OutClassInputIndex = INDEX_NONE;

		// TODO: assumes input node has exactly one input 
		if (ensure(InInputNode.Interface.Inputs.Num() == 1))
		{
			const FName& TypeName = InInputNode.Interface.Inputs[0].TypeName;

			auto IsMatchingInput = [&](const FMetasoundFrontendClassInput& GraphInput)
			{
				return (InInputNode.ID == GraphInput.NodeID);
			};

			OutClassInputIndex = InOwningGraph.Interface.Inputs.IndexOfByPredicate(IsMatchingInput);
			if (INDEX_NONE != OutClassInputIndex)
			{
				return &InOwningGraph.Interface.Inputs[OutClassInputIndex];
			}
		}
		return nullptr;
	}

	const FMetasoundFrontendClassOutput* FFrontendGraphBuilder::FindClassOutputForOutputNode(const FMetasoundFrontendGraphClass& InOwningGraph, const FMetasoundFrontendNode& InOutputNode, int32& OutClassOutputIndex) const
	{
		OutClassOutputIndex = INDEX_NONE;

		// TODO: assumes input node has exactly one input 
		if (ensure(InOutputNode.Interface.Outputs.Num() == 1))
		{
			const FName& TypeName = InOutputNode.Interface.Outputs[0].TypeName;

			auto IsMatchingOutput = [&](const FMetasoundFrontendClassOutput& GraphOutput)
			{
				return (InOutputNode.ID == GraphOutput.NodeID);
			};

			OutClassOutputIndex = InOwningGraph.Interface.Outputs.IndexOfByPredicate(IsMatchingOutput);
			if (INDEX_NONE != OutClassOutputIndex)
			{
				return &InOwningGraph.Interface.Outputs[OutClassOutputIndex];
			}
		}
		return nullptr;
	}


	const FMetasoundFrontendLiteral* FFrontendGraphBuilder::FindInputLiteralForInputNode(const FMetasoundFrontendNode& InInputNode, const FMetasoundFrontendClass& InInputNodeClass, const FMetasoundFrontendClassInput& InOwningGraphClassInput) const
	{
		// Default value priority is:
		// 1. A value set directly on the node
		// 2. A default value of the owning graph
		// 3. A default value on the input node class.

		const FMetasoundFrontendLiteral* Literal = nullptr;

		// Check for default value directly on node.
		if (ensure(InInputNode.Interface.Inputs.Num() == 1))
		{
			const FMetasoundFrontendVertex& InputVertex = InInputNode.Interface.Inputs[0];

			// Find input literal matching VerteXID
			const FMetasoundFrontendVertexLiteral* VertexLiteral = InInputNode.InputLiterals.FindByPredicate(
				[&](const FMetasoundFrontendVertexLiteral& InVertexLiteral)
				{
					return InVertexLiteral.VertexID == InputVertex.VertexID;
				}
			);

			if (nullptr != VertexLiteral)
			{
				Literal = &VertexLiteral->Value;
			}
		}

		// Check for default value on owning graph.
		if (nullptr == Literal)
		{
			// Find Class Default that is not invalid
			if (InOwningGraphClassInput.DefaultLiteral.IsValid())
			{
				Literal = &InOwningGraphClassInput.DefaultLiteral;
			}
		}

		// Check for default value on input node class
		if (nullptr == Literal && ensure(InInputNodeClass.Interface.Inputs.Num() == 1))
		{
			const FMetasoundFrontendClassInput& InputNodeClassInput = InInputNodeClass.Interface.Inputs[0];

			if (InputNodeClassInput.DefaultLiteral.IsValid())
			{
				Literal = &InputNodeClassInput.DefaultLiteral;
			}
		}

		return Literal;
	}

	// TODO: add errors here. Most will be a "PromptIfMissing"...
	void FFrontendGraphBuilder::AddNodesToGraph(const FMetasoundFrontendGraphClass& InGraphClass, const TMap<FGuid, const FMetasoundFrontendClass*>& InClasses, FFrontendGraph& OutGraph) const
	{
		for (const FMetasoundFrontendNode& Node : InGraphClass.Graph.Nodes)
		{
			const FMetasoundFrontendClass* NodeClass = InClasses.FindRef(Node.ClassID);

			if (ensure(nullptr != NodeClass))
			{
				switch (NodeClass->Metadata.Type)
				{
					case EMetasoundFrontendClassType::Input:
						{
							int32 InputIndex = INDEX_NONE;
							const FMetasoundFrontendClassInput* ClassInput = FindClassInputForInputNode(InGraphClass, Node, InputIndex);

							if ((nullptr != ClassInput) && (INDEX_NONE != InputIndex))
							{

								OutGraph.AddInputNode(Node.ID, InputIndex, ClassInput->Name, CreateInputNode(Node, *NodeClass, *ClassInput));
							}
							else
							{
								UE_LOG(LogMetasound, Error, TEXT("Failed to match input node [NodeID:%s, NodeName:%s] to owning graph [ClassID:%s] output."), *Node.ID.ToString(), *Node.Name, *InGraphClass.ID.ToString());
							}
						}

						break;

					case EMetasoundFrontendClassType::Output:
						{
							int32 OutputIndex = INDEX_NONE;
							const FMetasoundFrontendClassOutput* ClassOutput = FindClassOutputForOutputNode(InGraphClass, Node, OutputIndex);

							if ((nullptr != ClassOutput) && (INDEX_NONE != OutputIndex))
							{
								OutGraph.AddOutputNode(Node.ID, OutputIndex, ClassOutput->Name, CreateOutputNode(Node, *NodeClass));
							}
							else
							{
								UE_LOG(LogMetasound, Error, TEXT("Failed to match output node [NodeID:%s, NodeName:%s] to owning graph [ClassID:%s] output."), *Node.ID.ToString(), *Node.Name, *InGraphClass.ID.ToString());
							}
						}

						break;

					default:
						OutGraph.AddNode(Node.ID, CreateExternalNode(Node, *NodeClass));
				}
			}
		}
	}

	void FFrontendGraphBuilder::AddEdgesToGraph(const FMetasoundFrontendGraph& InFrontendGraph, FFrontendGraph& OutGraph) const
	{
		// Pair of frontend node and core node. The frontend node can be one of
		// several types.
		struct FCoreNodeAndFrontendVertex
		{
			const INode* Node = nullptr;
			const FMetasoundFrontendVertex* Vertex = nullptr;
		};

		// TODO: add support for array vertices.
		typedef TTuple<FGuid, FGuid> FNodeIDVertexID;

		TMap<FNodeIDVertexID, FCoreNodeAndFrontendVertex> NodeSourcesByID;
		TMap<FNodeIDVertexID, FCoreNodeAndFrontendVertex> NodeDestinationsByID;

		// Add nodes to NodeID/VertexID map
		for (const FMetasoundFrontendNode& Node : InFrontendGraph.Nodes)
		{
			const INode* CoreNode = OutGraph.FindNode(Node.ID);
			if (nullptr == CoreNode)
			{
				UE_LOG(LogMetasound, Display, TEXT("Could not find referenced node [NodeID:%s]"), *Node.ID.ToString());
				continue;
			}

			for (const FMetasoundFrontendVertex& Vertex : Node.Interface.Inputs)
			{
				NodeDestinationsByID.Add(FNodeIDVertexID(Node.ID, Vertex.VertexID), FCoreNodeAndFrontendVertex({CoreNode, &Vertex}));
			}

			for (const FMetasoundFrontendVertex& Vertex : Node.Interface.Outputs)
			{
				NodeSourcesByID.Add(FNodeIDVertexID(Node.ID, Vertex.VertexID), FCoreNodeAndFrontendVertex({CoreNode, &Vertex}));
			}
		};
		

		for (const FMetasoundFrontendEdge& Edge : InFrontendGraph.Edges)
		{
			const FNodeIDVertexID DestinationKey(Edge.ToNodeID, Edge.ToVertexID);
			const FCoreNodeAndFrontendVertex* DestinationNodeAndVertex = NodeDestinationsByID.Find(DestinationKey);

			if (nullptr == DestinationNodeAndVertex)
			{
				// TODO: bubble up error
				UE_LOG(LogMetasound, Error, TEXT("Failed to add edge. Could not find destination [NodeID:%s, VertexID:%s]"), *Edge.ToNodeID.ToString(), *Edge.ToVertexID.ToString());
				continue;
			}

			if (nullptr == DestinationNodeAndVertex->Node)
			{
				// TODO: bubble up error
				UE_LOG(LogMetasound, Warning, TEXT("Skipping edge. Null destination node [NodeID:%s]"), *Edge.ToNodeID.ToString());
				continue;
			}

			const FNodeIDVertexID SourceKey(Edge.FromNodeID, Edge.FromVertexID);
			const FCoreNodeAndFrontendVertex* SourceNodeAndVertex = NodeSourcesByID.Find(SourceKey);

			if (nullptr == SourceNodeAndVertex)
			{
				UE_LOG(LogMetasound, Error, TEXT("Failed to add edge. Could not find source [NodeID:%s, VertexID:%s]"), *Edge.FromNodeID.ToString(), *Edge.FromVertexID.ToString());
				continue;
			}

			if (nullptr == SourceNodeAndVertex->Node)
			{
				// TODO: bubble up error
				UE_LOG(LogMetasound, Warning, TEXT("Skipping edge. Null source node [NodeID:%s]"), *Edge.FromNodeID.ToString());
				continue;
			}

			const INode* FromNode = SourceNodeAndVertex->Node;
			const FVertexKey FromVertexKey = SourceNodeAndVertex->Vertex->Name;

			const INode* ToNode = DestinationNodeAndVertex->Node;
			const FVertexKey ToVertexKey = DestinationNodeAndVertex->Vertex->Name;

			bool bSuccess = OutGraph.AddDataEdge(*FromNode, FromVertexKey,  *ToNode, ToVertexKey);

			if (!bSuccess)
			{
				UE_LOG(LogMetasound, Error, TEXT("Failed to connect edge from [NodeID:%s, VertexID:%s] to [NodeID:%s, VertexID:%s]"), *Edge.FromNodeID.ToString(), *Edge.FromVertexID.ToString(), *Edge.ToNodeID.ToString(), *Edge.ToVertexID.ToString());
			}
		}
	}

	/** Check that all dependencies are C++ class dependencies. */
	bool FFrontendGraphBuilder::IsFlat(const FMetasoundFrontendDocument& InDocument) const
	{
		if (InDocument.Subgraphs.Num() > 0)
		{
			return false;
		}

		return IsFlat(InDocument.RootGraph, InDocument.Dependencies);
	}

	bool FFrontendGraphBuilder::IsFlat(const FMetasoundFrontendGraphClass& InRoot, const TArray<FMetasoundFrontendClass>& InDependencies) const
	{
		// All dependencies are external dependencies in a flat graph
		auto IsClassExternal = [&](const FMetasoundFrontendClass& InDesc) 
		{ 
			bool bIsExternal = (InDesc.Metadata.Type == EMetasoundFrontendClassType::External) ||
				(InDesc.Metadata.Type == EMetasoundFrontendClassType::Input) ||
				(InDesc.Metadata.Type == EMetasoundFrontendClassType::Output);
			return bIsExternal;
		};
		const bool bIsEveryDependencyExternal = Algo::AllOf(InDependencies, IsClassExternal);

		if (!bIsEveryDependencyExternal)
		{
			return false;
		}

		// All the dependencies are met 
		TSet<FGuid> AvailableDependencies;
		Algo::Transform(InDependencies, AvailableDependencies, [](const FMetasoundFrontendClass& InDesc) { return InDesc.ID; });

		auto IsDependencyMet = [&](const FMetasoundFrontendNode& InNode) 
		{ 
			return AvailableDependencies.Contains(InNode.ClassID);
		};

		const bool bIsEveryDependencyMet = Algo::AllOf(InRoot.Graph.Nodes, IsDependencyMet);

		return bIsEveryDependencyMet;
	}

	TUniquePtr<FFrontendGraph> FFrontendGraphBuilder::CreateGraph(const FMetasoundFrontendGraphClass& InGraph, const TArray<FMetasoundFrontendGraphClass>& InSubgraphs, const TArray<FMetasoundFrontendClass>& InDependencies) const
	{
		if (!IsFlat(InGraph, InDependencies))
		{
			// Likely this will change in the future and the builder will be able
			// to build graphs with subgraphs..
			UE_LOG(LogMetasound, Error, TEXT("Provided graph not flat. FFrontendGraphBuilder can only build flat graphs"));
			return TUniquePtr<FFrontendGraph>(nullptr);
		}


		TMap<FGuid, const FMetasoundFrontendClass*> ClassMap;

		for (const FMetasoundFrontendClass& ExtClass : InDependencies)
		{
			ClassMap.Add(ExtClass.ID, &ExtClass);
		}

		TUniquePtr<FFrontendGraph> MetasoundGraph = MakeUnique<FFrontendGraph>(InGraph.Metadata.ClassName.GetFullName().ToString(), FGuid::NewGuid());

		// TODO: will likely want to bubble up errors here for case where
		// a datatype or node is not registered. 
		AddNodesToGraph(InGraph, ClassMap, *MetasoundGraph);

		AddEdgesToGraph(InGraph.Graph, *MetasoundGraph);

		check(MetasoundGraph->OwnsAllReferencedNodes());

		return MoveTemp(MetasoundGraph);
	}
	
	/* Metasound document should be inflated by now. */
	TUniquePtr<FFrontendGraph> FFrontendGraphBuilder::CreateGraph(const FMetasoundFrontendDocument& InDocument) const
	{
		return CreateGraph(InDocument.RootGraph, InDocument.Subgraphs, InDocument.Dependencies);
	}
}
