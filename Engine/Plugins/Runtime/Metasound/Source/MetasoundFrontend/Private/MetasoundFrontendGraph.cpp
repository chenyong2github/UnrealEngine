// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendGraph.h"

#include "Algo/AllOf.h"
#include "Algo/AnyOf.h"
#include "Algo/TopologicalSort.h"
#include "Algo/Transform.h"
#include "CoreMinimal.h"
#include "MetasoundFrontend.h"
#include "MetasoundGraph.h"
#include "MetasoundLog.h"
#include "MetasoundNodeInterface.h"

namespace Metasound
{
	namespace FrontendGraphPrivate
	{
		FNodeInitData CreateNodeInitData(const FMetasoundFrontendNode& InNode)
		{
			FNodeInitData InitData;

			InitData.InstanceName.Append(InNode.Name);
			InitData.InstanceName.AppendChar('_');
			InitData.InstanceName.Append(InNode.ID.ToString());

			InitData.InstanceID = InNode.ID;

			return InitData;
		}
	}

	FFrontendGraph::FFrontendGraph(const FString& InInstanceName, const FGuid& InInstanceID)
	:	FGraph(InInstanceName, InInstanceID)
	{
	}

	void FFrontendGraph::AddInputNode(FGuid InDependencyId, int32 InIndex, const FVertexKey& InVertexKey, TSharedPtr<const INode> InNode)
	{
		if (InNode.IsValid())
		{
			// There shouldn't be duplicate IDs. 
			check(!InputNodes.Contains(InIndex));

			// Input nodes need an extra Index value to keep track of their position in the graph's inputs.
			InputNodes.Add(InIndex, InNode.Get());
			AddInputDataDestination(*InNode, InVertexKey);
			AddNode(InDependencyId, InNode);
		}
	}

	void FFrontendGraph::AddOutputNode(FGuid InNodeID, int32 InIndex, const FVertexKey& InVertexKey, TSharedPtr<const INode> InNode)
	{
		if (InNode.IsValid())
		{
			// There shouldn't be duplicate IDs. 
			check(!OutputNodes.Contains(InIndex));

			// Output nodes need an extra Index value to keep track of their position in the graph's inputs.
			OutputNodes.Add(InIndex, InNode.Get());
			AddOutputDataSource(*InNode, InVertexKey);
			AddNode(InNodeID, InNode);
		}
	}

	/** Store a node on this graph. */
	void FFrontendGraph::AddNode(FGuid InNodeID, TSharedPtr<const INode> InNode)
	{
		if (InNode.IsValid())
		{
			// There shouldn't be duplicate IDs. 
			check(!NodeMap.Contains(InNodeID));

			NodeMap.Add(InNodeID, InNode.Get());
			StoreNode(InNode);
		}
	}

	const INode* FFrontendGraph::FindNode(FGuid InNodeID) const
	{
		const INode* const* NodePtr = NodeMap.Find(InNodeID);

		if (nullptr != NodePtr)
		{
			return *NodePtr;
		}

		return nullptr;
	}

	const INode* FFrontendGraph::FindInputNode(int32 InIndex) const
	{
		const INode* const* NodePtr = InputNodes.Find(InIndex);

		if (nullptr != NodePtr)
		{
			return *NodePtr;
		}

		return nullptr;
	}

	const INode* FFrontendGraph::FindOutputNode(int32 InIndex) const
	{
		const INode* const* NodePtr = OutputNodes.Find(InIndex);

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

	void FFrontendGraph::StoreNode(TSharedPtr<const INode> InNode)
	{
		check(InNode.IsValid());
		StoredNodes.Add(InNode.Get());
		NodeStorage.Add(InNode);
	}

	TUniquePtr<INode> FFrontendGraphBuilder::CreateInputNode(const FMetasoundFrontendNode& InNode, const FMetasoundFrontendClass& InClass, const FMetasoundFrontendClassInput& InOwningGraphClassInput)
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
					UE_LOG(LogMetaSound, Error, TEXT("Cannot create input node [NodeID:%s]. [Vertex:%s] cannot be constructed with the provided literal type."), *InNode.ID.ToString(), *InputVertex.Name);
				}
			}
		}
		else
		{
			UE_LOG(LogMetaSound, Error, TEXT("Cannot create input node [NodeID:%s]. No default literal set for input node."), *InNode.ID.ToString());
		}

		return TUniquePtr<INode>(nullptr);
	}

	TUniquePtr<INode> FFrontendGraphBuilder::CreateOutputNode(const FMetasoundFrontendNode& InNode, const FMetasoundFrontendClass& InClass, FBuildGraphContext& InGraphContext)
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
				OutputVertex.Name
			};

			{
				const FNodeInitData InitData = FrontendGraphPrivate::CreateNodeInitData(InNode);
				TArray<FDefaultVariableData> DefaultVariableData = GetInputDefaultVariableData(InNode, InitData);
				for (FDefaultVariableData& Data : DefaultVariableData)
				{
					InGraphContext.DefaultInputs.Emplace(FNodeIDVertexID { InNode.ID, Data.DestinationVertexID }, MoveTemp(Data));
				}
			}

			// TODO: create output node using external class definition
			return FMetasoundFrontendRegistryContainer::Get()->ConstructOutputNode(OutputVertex.TypeName, MoveTemp(InitParams));
		}
		return TUniquePtr<INode>(nullptr);
	}

	TUniquePtr<INode> FFrontendGraphBuilder::CreateExternalNode(const FMetasoundFrontendNode& InNode, const FMetasoundFrontendClass& InClass, FBuildGraphContext& InGraphContext)
	{
		check(InClass.Metadata.Type == EMetasoundFrontendClassType::External);
		check(InNode.ClassID == InClass.ID);

		const FNodeInitData InitData = FrontendGraphPrivate::CreateNodeInitData(InNode);
		{
			TArray<FDefaultVariableData> DefaultVariableData = GetInputDefaultVariableData(InNode, InitData);
			for (FDefaultVariableData& Data : DefaultVariableData)
			{
				InGraphContext.DefaultInputs.Emplace(FNodeIDVertexID { InNode.ID, Data.DestinationVertexID }, MoveTemp(Data));
			}
		}

		// TODO: handle check to see if node interface conforms to class interface here. 
		// TODO: check to see if external object supports class interface.

		Metasound::Frontend::FNodeRegistryKey Key = FMetasoundFrontendRegistryContainer::GetRegistryKey(InClass.Metadata);

		return FMetasoundFrontendRegistryContainer::Get()->ConstructExternalNode(Key, InitData);
	}

	const FMetasoundFrontendClassInput* FFrontendGraphBuilder::FindClassInputForInputNode(const FMetasoundFrontendGraphClass& InOwningGraph, const FMetasoundFrontendNode& InInputNode, int32& OutClassInputIndex)
	{
		OutClassInputIndex = INDEX_NONE;

		// Input nodes should have exactly one input.
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

	const FMetasoundFrontendClassOutput* FFrontendGraphBuilder::FindClassOutputForOutputNode(const FMetasoundFrontendGraphClass& InOwningGraph, const FMetasoundFrontendNode& InOutputNode, int32& OutClassOutputIndex)
	{
		OutClassOutputIndex = INDEX_NONE;

		// Output nodes should have exactly one output
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

	const FMetasoundFrontendLiteral* FFrontendGraphBuilder::FindInputLiteralForInputNode(const FMetasoundFrontendNode& InInputNode, const FMetasoundFrontendClass& InInputNodeClass, const FMetasoundFrontendClassInput& InOwningGraphClassInput)
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
	void FFrontendGraphBuilder::AddNodesToGraph(FBuildGraphContext& InGraphContext)
	{
		for (const FMetasoundFrontendNode& Node : InGraphContext.GraphClass.Graph.Nodes)
		{
			const FMetasoundFrontendClass* NodeClass = InGraphContext.BuildContext.FrontendClasses.FindRef(Node.ClassID);

			if (ensure(nullptr != NodeClass))
			{
				switch (NodeClass->Metadata.Type)
				{
					case EMetasoundFrontendClassType::Input:
					{
						int32 InputIndex = INDEX_NONE;
						const FMetasoundFrontendClassInput* ClassInput = FindClassInputForInputNode(InGraphContext.GraphClass, Node, InputIndex);

						if ((nullptr != ClassInput) && (INDEX_NONE != InputIndex))
						{
							TSharedPtr<const INode> InputNode(CreateInputNode(Node, *NodeClass, *ClassInput).Release());
							InGraphContext.Graph->AddInputNode(Node.ID, InputIndex, ClassInput->Name, InputNode);
						}
						else
						{
							const FString GraphClassIDString = InGraphContext.GraphClass.ID.ToString();
							UE_LOG(LogMetaSound, Error, TEXT("Failed to match input node [NodeID:%s, NodeName:%s] to owning graph [ClassID:%s] output."), *Node.ID.ToString(), *Node.Name, *GraphClassIDString);
						}
					}
					break;

					case EMetasoundFrontendClassType::Output:
					{
						int32 OutputIndex = INDEX_NONE;
						const FMetasoundFrontendClassOutput* ClassOutput = FindClassOutputForOutputNode(InGraphContext.GraphClass, Node, OutputIndex);
						if ((nullptr != ClassOutput) && (INDEX_NONE != OutputIndex))
						{
							TSharedPtr<const INode> OutputNode(CreateOutputNode(Node, *NodeClass, InGraphContext).Release());
							InGraphContext.Graph->AddOutputNode(Node.ID, OutputIndex, ClassOutput->Name, OutputNode);
						}
						else
						{
							const FString GraphClassIDString = InGraphContext.GraphClass.ID.ToString();
							UE_LOG(LogMetaSound, Error, TEXT("Failed to match output node [NodeID:%s, NodeName:%s] to owning graph [ClassID:%s] output."), *Node.ID.ToString(), *Node.Name, *GraphClassIDString);
						}
					}
					break;

					case EMetasoundFrontendClassType::Variable:
					{
						ensureAlwaysMsgf(false, TEXT("TODO: Implement ability to create variables directly in Frontend"));
					}

					break;

					case EMetasoundFrontendClassType::Graph:
					{
						const TSharedPtr<const INode> SubgraphPtr = InGraphContext.BuildContext.Graphs.FindRef(Node.ClassID);

						if (SubgraphPtr.IsValid())
						{
							InGraphContext.Graph->AddNode(Node.ID, SubgraphPtr);
						}
						else
						{
							UE_LOG(LogMetaSound, Error, TEXT("Failed to find subgraph for node [NodeID:%s, NodeName:%s, ClassID:%s]"), *Node.ID.ToString(), *Node.Name, *Node.ClassID.ToString());
						}
					}
					break;

					case EMetasoundFrontendClassType::External:
					default:
					{
						TSharedPtr<const INode> ExternalNode(CreateExternalNode(Node, *NodeClass, InGraphContext).Release());
						InGraphContext.Graph->AddNode(Node.ID, ExternalNode);
					}
					break;
				}
			}
		}
	}

	void FFrontendGraphBuilder::AddEdgesToGraph(FBuildGraphContext& InGraphContext)
	{
		// Pair of frontend node and core node. The frontend node can be one of
		// several types.
		struct FCoreNodeAndFrontendVertex
		{
			const INode* Node = nullptr;
			const FMetasoundFrontendVertex* Vertex = nullptr;
		};

		TMap<FNodeIDVertexID, FCoreNodeAndFrontendVertex> NodeSourcesByID;
		TMap<FNodeIDVertexID, FCoreNodeAndFrontendVertex> NodeDestinationsByID;

		// Add nodes to NodeID/VertexID map
		for (const FMetasoundFrontendNode& Node : InGraphContext.GraphClass.Graph.Nodes)
		{
			const INode* CoreNode = InGraphContext.Graph->FindNode(Node.ID);
			if (nullptr == CoreNode)
			{
				UE_LOG(LogMetaSound, Warning, TEXT("Could not find referenced node [Name:%s, NodeID:%s]"), *Node.Name, *Node.ID.ToString());
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

		for (const FMetasoundFrontendEdge& Edge : InGraphContext.GraphClass.Graph.Edges)
		{
			const FNodeIDVertexID DestinationKey(Edge.ToNodeID, Edge.ToVertexID);
			const FCoreNodeAndFrontendVertex* DestinationNodeAndVertex = NodeDestinationsByID.Find(DestinationKey);

			if (nullptr == DestinationNodeAndVertex)
			{
				// TODO: bubble up error
				UE_LOG(LogMetaSound, Error, TEXT("Failed to add edge. Could not find destination [NodeID:%s, VertexID:%s]"), *Edge.ToNodeID.ToString(), *Edge.ToVertexID.ToString());
				continue;
			}

			if (nullptr == DestinationNodeAndVertex->Node)
			{
				// TODO: bubble up error
				UE_LOG(LogMetaSound, Warning, TEXT("Skipping edge. Null destination node [NodeID:%s]"), *Edge.ToNodeID.ToString());
				continue;
			}

			const FNodeIDVertexID SourceKey(Edge.FromNodeID, Edge.FromVertexID);
			const FCoreNodeAndFrontendVertex* SourceNodeAndVertex = NodeSourcesByID.Find(SourceKey);

			if (nullptr == SourceNodeAndVertex)
			{
				UE_LOG(LogMetaSound, Error, TEXT("Failed to add edge. Could not find source [NodeID:%s, VertexID:%s]"), *Edge.FromNodeID.ToString(), *Edge.FromVertexID.ToString());
				continue;
			}

			if (nullptr == SourceNodeAndVertex->Node)
			{
				// TODO: bubble up error
				UE_LOG(LogMetaSound, Warning, TEXT("Skipping edge. Null source node [NodeID:%s]"), *Edge.FromNodeID.ToString());
				continue;
			}

			const INode* FromNode = SourceNodeAndVertex->Node;
			const FVertexKey FromVertexKey = SourceNodeAndVertex->Vertex->Name;

			const INode* ToNode = DestinationNodeAndVertex->Node;
			const FVertexKey ToVertexKey = DestinationNodeAndVertex->Vertex->Name;

			bool bSuccess = InGraphContext.Graph->AddDataEdge(*FromNode, FromVertexKey,  *ToNode, ToVertexKey);

			// If succeeded, remove input as viable vertex to construct default variable, as it has been superceded by a connection.
			if (bSuccess)
			{
				FNodeIDVertexID DestinationPair { ToNode->GetInstanceID(), DestinationNodeAndVertex->Vertex->VertexID };
				InGraphContext.DefaultInputs.Remove(DestinationPair);
			}
			else
			{
				UE_LOG(LogMetaSound, Error, TEXT("Failed to connect edge from [NodeID:%s, VertexID:%s] to [NodeID:%s, VertexID:%s]"), *Edge.FromNodeID.ToString(), *Edge.FromVertexID.ToString(), *Edge.ToNodeID.ToString(), *Edge.ToVertexID.ToString());
			}
		}
	}

	void FFrontendGraphBuilder::AddDefaultInputVariables(FBuildGraphContext& InGraphContext)
	{
		for (const TPair<FNodeIDVertexID, FDefaultVariableData>& Pair : InGraphContext.DefaultInputs)
		{
			const FDefaultVariableData& VariableData = Pair.Value;
			const FGuid VariableNodeID = FGuid::NewGuid();

			// 1. Construct and add the default variable to the graph
			{
				FVariableNodeConstructorParams InitParams = VariableData.InitParams.Clone();
				TUniquePtr<const INode> DefaultVariable = FMetasoundFrontendRegistryContainer::Get()->ConstructVariableNode(VariableData.TypeName, MoveTemp(InitParams));
				InGraphContext.Graph->AddNode(VariableNodeID, TSharedPtr<const INode>(DefaultVariable.Release()));
			}

			// 2. Connect the default variable to the expected input
			const INode* FromNode = InGraphContext.Graph->FindNode(VariableNodeID);
			if (!ensure(FromNode))
			{
				continue;
			}
			const FVertexKey& FromVertexKey = VariableData.InitParams.VertexName;

			const INode* ToNode = InGraphContext.Graph->FindNode(VariableData.DestinationNodeID);
			if (!ensure(ToNode))
			{
				continue;
			}
			const FVertexKey& ToVertexKey = VariableData.DestinationVertexKey;

			bool bSuccess = InGraphContext.Graph->AddDataEdge(*FromNode, FromVertexKey, *ToNode, ToVertexKey);
			if (!bSuccess)
			{
				UE_LOG(LogMetaSound, Error, TEXT("Failed to connect default variable edge: from '%s' to '%s'"), *FromVertexKey, *ToVertexKey);
			}
		}
	}

	TArray<FFrontendGraphBuilder::FDefaultVariableData> FFrontendGraphBuilder::GetInputDefaultVariableData(const FMetasoundFrontendNode& InNode, const FNodeInitData& InInitData)
	{
		TArray<FDefaultVariableData> DefaultVariableData;

		TArray<FMetasoundFrontendVertex> InputVertices = InNode.Interface.Inputs;
		for (const FMetasoundFrontendVertexLiteral& Literal : InNode.InputLiterals)
		{
			FVariableNodeConstructorParams InitParams;
			FName TypeName;

			FGuid VertexID = Literal.VertexID;
			FVertexKey DestinationVertexKey;
			auto RemoveAndBuildParams = [&](const FMetasoundFrontendVertex& Vertex)
			{
				if (Vertex.VertexID == VertexID)
				{
					InitParams.InitParam = Literal.Value.ToLiteral(Vertex.TypeName);
					InitParams.InstanceID = FGuid::NewGuid();
					InitParams.NodeName = InInitData.InstanceName + Vertex.Name + InitParams.InstanceID.ToString();
					InitParams.VertexName = InitParams.NodeName;
					TypeName = Vertex.TypeName;

					DestinationVertexKey = Vertex.Name;

					return true;
				}

				return false;
			};

			const bool bRemoved = InputVertices.RemoveAllSwap(RemoveAndBuildParams, false /* bAllowShrinking */) > 0;
			if (ensure(bRemoved))
			{
				DefaultVariableData.Emplace(FDefaultVariableData
				{
					InNode.ID,
					VertexID,
					DestinationVertexKey,
					TypeName,
					MoveTemp(InitParams)
				});
			}
		}

		return DefaultVariableData;
	}

	/** Check that all dependencies are C++ class dependencies. */
	bool FFrontendGraphBuilder::IsFlat(const FMetasoundFrontendDocument& InDocument)
	{
		if (InDocument.Subgraphs.Num() > 0)
		{
			return false;
		}

		return IsFlat(InDocument.RootGraph, InDocument.Dependencies);
	}

	bool FFrontendGraphBuilder::IsFlat(const FMetasoundFrontendGraphClass& InRoot, const TArray<FMetasoundFrontendClass>& InDependencies)
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

	bool FFrontendGraphBuilder::SortSubgraphDependencies(TArray<const FMetasoundFrontendGraphClass*>& Subgraphs)
	{
		// Helper for caching and querying subgraph dependencies
		struct FSubgraphDependencyLookup
		{
			FSubgraphDependencyLookup(TArrayView<const FMetasoundFrontendGraphClass*> InGraphs)
			{
				// Map ClassID to graph pointer. 
				TMap<FGuid, const FMetasoundFrontendGraphClass*> ClassIDAndGraph;
				for (const FMetasoundFrontendGraphClass* Graph: InGraphs)
				{
					ClassIDAndGraph.Add(Graph->ID, Graph);
				}

				// Cache subgraph dependencies.
				for (const FMetasoundFrontendGraphClass* GraphClass : InGraphs)
				{
					for (const FMetasoundFrontendNode& Node : GraphClass->Graph.Nodes)
					{
						if (ClassIDAndGraph.Contains(Node.ClassID))
						{
							DependencyMap.Add(GraphClass, ClassIDAndGraph[Node.ClassID]);
						}
					}
				}
			}

			TArray<const FMetasoundFrontendGraphClass*> operator()(const FMetasoundFrontendGraphClass* InParent) const
			{
				TArray<const FMetasoundFrontendGraphClass*> Dependencies;
				DependencyMap.MultiFind(InParent, Dependencies);
				return Dependencies;
			}

		private:

			TMultiMap<const FMetasoundFrontendGraphClass*, const FMetasoundFrontendGraphClass*> DependencyMap;
		};

		bool bSuccess = Algo::TopologicalSort(Subgraphs, FSubgraphDependencyLookup(Subgraphs));
		if (!bSuccess)
		{
			UE_LOG(LogMetaSound, Error, TEXT("Failed to topologically sort subgraphs. Possible recursive subgraph dependency"));
		}

		return bSuccess;
	}

	TUniquePtr<FFrontendGraph> FFrontendGraphBuilder::CreateGraph(FBuildContext& InContext, const FMetasoundFrontendGraphClass& InGraphClass)
	{
		const FString GraphName = InGraphClass.Metadata.ClassName.GetFullName().ToString();

		FBuildGraphContext BuildGraphContext
		{
			MakeUnique<FFrontendGraph>(GraphName, FGuid::NewGuid()),
			InGraphClass,
			InContext
		};

		// TODO: will likely want to bubble up errors here for case where
		// a datatype or node is not registered.
		AddNodesToGraph(BuildGraphContext);
		AddEdgesToGraph(BuildGraphContext);
		AddDefaultInputVariables(BuildGraphContext);

		check(BuildGraphContext.Graph->OwnsAllReferencedNodes());
		return MoveTemp(BuildGraphContext.Graph);
	}

	TUniquePtr<FFrontendGraph> FFrontendGraphBuilder::CreateGraph(const FMetasoundFrontendGraphClass& InGraph, const TArray<FMetasoundFrontendGraphClass>& InSubgraphs, const TArray<FMetasoundFrontendClass>& InDependencies)
	{
		FBuildContext Context;

		// Gather all references to node classes from external dependencies and subgraphs.
		for (const FMetasoundFrontendClass& ExtClass : InDependencies)
		{
			Context.FrontendClasses.Add(ExtClass.ID, &ExtClass);
		}
		for (const FMetasoundFrontendClass& ExtClass : InSubgraphs)
		{
			Context.FrontendClasses.Add(ExtClass.ID, &ExtClass);
		}

		// Sort subgraphs so that dependent subgraphs are created in correct order.
		TArray<const FMetasoundFrontendGraphClass*> FrontendSubgraphPtrs;
		Algo::Transform(InSubgraphs, FrontendSubgraphPtrs, [](const FMetasoundFrontendGraphClass& InClass) { return &InClass; });

		bool bSuccess = SortSubgraphDependencies(FrontendSubgraphPtrs);
		if (!bSuccess)
		{
			UE_LOG(LogMetaSound, Error, TEXT("Failed to create graph due to failed subgraph ordering."));
			return TUniquePtr<FFrontendGraph>(nullptr);
		}

		// Create each subgraph.
		for (const FMetasoundFrontendGraphClass* FrontendSubgraphPtr : FrontendSubgraphPtrs)
		{
			TSharedPtr<const INode> Subgraph(CreateGraph(Context, *FrontendSubgraphPtr).Release());
			if (!Subgraph.IsValid())
			{
				UE_LOG(LogMetaSound, Warning, TEXT("Failed to create subgraph [SubgraphName: %s]"), *FrontendSubgraphPtr->Metadata.ClassName.ToString());
			}
			else
			{
				// Add subgraphs to context so they are accessible for subsequent graphs.
				Context.Graphs.Add(FrontendSubgraphPtr->ID, Subgraph);
			}
		}

		// Create parent graph.
		return CreateGraph(Context, InGraph);
	}
	
	/* Metasound document should be inflated by now. */
	TUniquePtr<FFrontendGraph> FFrontendGraphBuilder::CreateGraph(const FMetasoundFrontendDocument& InDocument)
	{
		return CreateGraph(InDocument.RootGraph, InDocument.Subgraphs, InDocument.Dependencies);
	}
}
