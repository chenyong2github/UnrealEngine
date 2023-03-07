// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendDocumentCache.h"

#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendDocumentBuilder.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundTrace.h"


namespace Metasound::Frontend
{
	FDocumentCache::FDocumentCache(const FMetasoundFrontendDocument& InDocument)
		: Document(&InDocument)
	{
		METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(FDocumentCache::FDocumentCache);

		for (int32 Index = 0; Index < Document->Dependencies.Num(); ++Index)
		{
			const FMetasoundFrontendClass& Class = Document->Dependencies[Index];
			const FMetasoundFrontendClassMetadata& Metadata = Class.Metadata;
			FNodeRegistryKey Key = NodeRegistryKey::CreateKey(Metadata);
			KeyToIndex.Add(MoveTemp(Key), Index);
			IDToIndex.Add(Class.ID, Index);
		}

		// Dependencies must be cached first as the NodeCache is dependent on them
		NodeCache = MakeUnique<FDocumentGraphNodeCache>(InDocument, *this);
		EdgeCache = MakeUnique<FDocumentGraphEdgeCache>(InDocument);
	}

	const FMetasoundFrontendDocument& FDocumentCache::GetDocument() const
	{
		check(Document);
		return *Document;
	}

	bool FDocumentCache::ContainsDependency(const FNodeRegistryKey& InClassKey) const
	{
		return KeyToIndex.Contains(InClassKey);
	}

	const FMetasoundFrontendClass* FDocumentCache::FindDependency(const FNodeRegistryKey& InClassKey) const
	{
		if (const int32* Index = KeyToIndex.Find(InClassKey))
		{
			return &GetDocument().Dependencies[*Index];
		}

		return nullptr;
	}

	const FMetasoundFrontendClass* FDocumentCache::FindDependency(const FGuid& InClassID) const
	{
		if (const int32* Index = IDToIndex.Find(InClassID))
		{
			return &GetDocument().Dependencies[*Index];
		}

		return nullptr;
	}

	const int32* FDocumentCache::FindDependencyIndex(const Metasound::Frontend::FNodeRegistryKey& InClassKey) const
	{
		return KeyToIndex.Find(InClassKey);
	}

	const int32* FDocumentCache::FindDependencyIndex(const FGuid& InClassID) const
	{
		return IDToIndex.Find(InClassID);
	}

	void FDocumentCache::OnDependencyAdded(int32 NewIndex)
	{
		using namespace Metasound::Frontend;

		const FMetasoundFrontendClass& Dependency = GetDocument().Dependencies[NewIndex];
		const FNodeRegistryKey Key = NodeRegistryKey::CreateKey(Dependency.Metadata);
		KeyToIndex.Add(Key, NewIndex);
		IDToIndex.Add(Dependency.ID, NewIndex);
	}

	void FDocumentCache::OnRemovingDependency(int32 IndexBeingRemoved)
	{
		using namespace Metasound::Frontend;

		const FMetasoundFrontendClass& DependencyBeingRemoved = GetDocument().Dependencies[IndexBeingRemoved];
		const FNodeRegistryKey Key = NodeRegistryKey::CreateKey(DependencyBeingRemoved.Metadata);
		KeyToIndex.Remove(Key);
		IDToIndex.Remove(DependencyBeingRemoved.ID);
	}

	FDocumentGraphNodeCache::FDocumentGraphNodeCache(const FMetasoundFrontendDocument& InDocument, const FDocumentCache& InParent)
		: Document(&InDocument)
		, Parent(&InParent)
	{
		const FMetasoundFrontendGraph& Graph = GetDocument().RootGraph.Graph;
		const TArray<FMetasoundFrontendNode>& Nodes = Graph.Nodes;
		for (int32 Index = 0; Index < Nodes.Num(); ++Index)
		{
			const FMetasoundFrontendNode& Node = Nodes[Index];
			const FMetasoundFrontendClass* Class = InParent.FindDependency(Node.ClassID);
			check(Class);

			IDToIndex.Add(Node.GetID(), Index);

			switch (Class->Metadata.GetType())
			{
				case EMetasoundFrontendClassType::Output:
				{
					OutputNameToIndex.Add(Node.Name, Index);
				}
				break;

				case EMetasoundFrontendClassType::Input:
				{
					InputNameToIndex.Add(Node.Name, Index);
				}
				break;

				default:
				break;
			}
		}
	}

	const FMetasoundFrontendDocument& FDocumentGraphNodeCache::GetDocument() const
	{
		check(Document);
		return *Document;
	}

	bool FDocumentGraphNodeCache::ContainsNode(const FGuid& InNodeID) const
	{
		return IDToIndex.Contains(InNodeID);
	}

	const int32* FDocumentGraphNodeCache::FindInputNodeIndex(FName InputName) const
	{
		return InputNameToIndex.Find(InputName);
	}

	const int32* FDocumentGraphNodeCache::FindOutputNodeIndex(FName OutputName) const
	{
		return OutputNameToIndex.Find(OutputName);
	}

	const int32* FDocumentGraphNodeCache::FindNodeIndex(const FGuid& InNodeID) const
	{
		return IDToIndex.Find(InNodeID);
	}

	TArray<const FMetasoundFrontendNode*> FDocumentGraphNodeCache::FindNodesOfClassID(const FGuid& InClassID) const
	{
		TArray<const FMetasoundFrontendNode*> Nodes;
		if (const TArray<int32>* NodeIndices = ClassIDToNodeIndices.Find(InClassID))
		{
			const FMetasoundFrontendGraph& Graph = GetDocument().RootGraph.Graph;
			Algo::Transform(*NodeIndices, Nodes, [&Graph](const int32& Index) { return &Graph.Nodes[Index]; });
		}
		return Nodes;
	}

	const FMetasoundFrontendNode* FDocumentGraphNodeCache::FindNode(const FGuid& InNodeID) const
	{
		if (const int32* NodeIndex = IDToIndex.Find(InNodeID))
		{
			return &GetDocument().RootGraph.Graph.Nodes[*NodeIndex];
		}

		return nullptr;
	}

	bool FDocumentGraphNodeCache::ContainsNodesOfClassID(const FGuid& InClassID) const
	{
		if (const TArray<int32>* NodeIndices = ClassIDToNodeIndices.Find(InClassID))
		{
			return !NodeIndices->IsEmpty();
		}

		return false;
	}

	TArray<const FMetasoundFrontendVertex*> FDocumentGraphNodeCache::FindNodeInputs(const FGuid& InNodeID, FName TypeName) const
	{
		TArray<const FMetasoundFrontendVertex*> FoundVertices;
		if (const FMetasoundFrontendNode* Node = FindNode(InNodeID))
		{
			auto IsVertexOfType = [&TypeName](const FMetasoundFrontendVertex& Vertex) { return TypeName.IsNone() || Vertex.TypeName == TypeName; };
			auto GetVertexPtr = [](const FMetasoundFrontendVertex& Vertex) { return &Vertex; };
			Algo::TransformIf(Node->Interface.Inputs, FoundVertices, IsVertexOfType, GetVertexPtr);
		}

		return FoundVertices;
	}

	TArray<const FMetasoundFrontendVertex*> FDocumentGraphNodeCache::FindNodeOutputs(const FGuid& InNodeID, FName TypeName) const
	{
		TArray<const FMetasoundFrontendVertex*> FoundVertices;
		if (const FMetasoundFrontendNode* Node = FindNode(InNodeID))
		{
			auto IsVertexOfType = [&TypeName](const FMetasoundFrontendVertex& Vertex) { return TypeName.IsNone() || Vertex.TypeName == TypeName; };
			auto GetVertexPtr = [](const FMetasoundFrontendVertex& Vertex) { return &Vertex; };
			Algo::TransformIf(Node->Interface.Outputs, FoundVertices, IsVertexOfType, GetVertexPtr);
		}

		return FoundVertices;
	}

	const FMetasoundFrontendNode* FDocumentGraphNodeCache::FindOutputNode(FName OutputName) const
	{
		if (const int32* Index = OutputNameToIndex.Find(OutputName))
		{
			return &GetDocument().RootGraph.Graph.Nodes[*Index];
		}

		return nullptr;
	}

	const FMetasoundFrontendNode* FDocumentGraphNodeCache::FindInputNode(FName InputName) const
	{
		if (const int32* Index = InputNameToIndex.Find(InputName))
		{
			return &GetDocument().RootGraph.Graph.Nodes[*Index];
		}

		return nullptr;
	}

	const FMetasoundFrontendVertex* FDocumentGraphNodeCache::FindInputVertex(const FGuid& InNodeID, const FGuid& InVertexID) const
	{
		if (const FMetasoundFrontendNode* Node = FindNode(InNodeID))
		{
			auto VertexMatchesPredicate = [&InVertexID](const FMetasoundFrontendVertex& Vertex)
			{
				return Vertex.VertexID == InVertexID;
			};
			return Node->Interface.Inputs.FindByPredicate(VertexMatchesPredicate);
		}

		return nullptr;
	}

	const FMetasoundFrontendVertex* FDocumentGraphNodeCache::FindInputVertex(const FGuid& InNodeID, FName InVertexName) const
	{
		if (const FMetasoundFrontendNode* Node = FindNode(InNodeID))
		{
			auto VertexMatchesPredicate = [&InVertexName](const FMetasoundFrontendVertex& Vertex)
			{
				return Vertex.Name == InVertexName;
			};
			return Node->Interface.Inputs.FindByPredicate(VertexMatchesPredicate);
		}

		return nullptr;
	}

	const FMetasoundFrontendVertex* FDocumentGraphNodeCache::FindOutputVertex(const FGuid& InNodeID, const FGuid& InVertexID) const
	{
		if (const FMetasoundFrontendNode* Node = FindNode(InNodeID))
		{
			auto VertexMatchesPredicate = [&InVertexID](const FMetasoundFrontendVertex& Vertex)
			{
				return Vertex.VertexID == InVertexID;
			};
			return Node->Interface.Outputs.FindByPredicate(VertexMatchesPredicate);
		}

		return nullptr;
	}

	const FMetasoundFrontendVertex* FDocumentGraphNodeCache::FindOutputVertex(const FGuid& InNodeID, FName InVertexName) const
	{
		if (const FMetasoundFrontendNode* Node = FindNode(InNodeID))
		{
			auto VertexMatchesPredicate = [&InVertexName](const FMetasoundFrontendVertex& Vertex)
			{
				return Vertex.Name == InVertexName;
			};
			return Node->Interface.Outputs.FindByPredicate(VertexMatchesPredicate);
		}

		return nullptr;
	}

	void FDocumentGraphNodeCache::OnNodeAdded(int32 InNewIndex)
	{
		const FMetasoundFrontendNode& Node = GetDocument().RootGraph.Graph.Nodes[InNewIndex];
		IDToIndex.Add(Node.GetID(), InNewIndex);
		ClassIDToNodeIndices.FindOrAdd(Node.ClassID).Add(InNewIndex);

		check(Parent);
		const FMetasoundFrontendClass* Dependency = Parent->FindDependency(Node.ClassID);
		check(Dependency);

		switch (Dependency->Metadata.GetType())
		{
			case EMetasoundFrontendClassType::Input:
			{
				InputNameToIndex.Add(Node.Name, InNewIndex);
			}
			break;

			case EMetasoundFrontendClassType::Output:
			{
				OutputNameToIndex.Add(Node.Name, InNewIndex);
			}
			break;

			default:
			break;
		}
	}

	void FDocumentGraphNodeCache::OnRemovingNode(int32 IndexBeingRemoved)
	{
		using namespace Metasound::Frontend;

		// If not found, transaction was missed
		const FMetasoundFrontendNode& NodeBeingRemoved = GetDocument().RootGraph.Graph.Nodes[IndexBeingRemoved];

		IDToIndex.Remove(NodeBeingRemoved.GetID());
		TArray<int32>& NodeIndices = ClassIDToNodeIndices.FindChecked(NodeBeingRemoved.ClassID);
		NodeIndices.Remove(IndexBeingRemoved);
		if (NodeIndices.IsEmpty())
		{
			ClassIDToNodeIndices.Remove(NodeBeingRemoved.ClassID);
		}

		check(Parent);
		const FMetasoundFrontendClass* Dependency = Parent->FindDependency(NodeBeingRemoved.ClassID);
		check(Dependency);
		switch (Dependency->Metadata.GetType())
		{
			case EMetasoundFrontendClassType::Input:
			{
				InputNameToIndex.Add(NodeBeingRemoved.Name);
			}
			break;

			case EMetasoundFrontendClassType::Output:
			{
				OutputNameToIndex.Remove(NodeBeingRemoved.Name);
			}
			break;

			default:
			break;
		}
	}

	FDocumentGraphEdgeCache::FDocumentGraphEdgeCache(const FMetasoundFrontendDocument& InDocument)
		: Document(&InDocument)
	{
		const TArray<FMetasoundFrontendEdge>& Edges = GetDocument().RootGraph.Graph.Edges;
		for (int32 Index = 0; Index < Edges.Num(); ++Index)
		{
			const FMetasoundFrontendEdge& Edge = Edges[Index];
			OutputToEdgeIndices.FindOrAdd(Edge.GetFromVertexHandle()).Add(Index);
			InputToEdgeIndex.Add(Edge.GetToVertexHandle()) = Index;
		}
	}

	bool FDocumentGraphEdgeCache::ContainsEdge(const FMetasoundFrontendEdge& InEdge) const
	{
		const FMetasoundFrontendVertexHandle InputPair = { InEdge.FromNodeID, InEdge.FromVertexID };
		if (const int32* Index = InputToEdgeIndex.Find(InputPair))
		{
			const FMetasoundFrontendEdge& Edge = GetDocument().RootGraph.Graph.Edges[*Index];
			return Edge.ToNodeID == InEdge.ToNodeID && Edge.ToVertexID == InEdge.ToVertexID;
		}

		return false;
	}

	TArray<const FMetasoundFrontendEdge*> FDocumentGraphEdgeCache::FindEdges(const FGuid& InNodeID, const FGuid& InVertexID) const
	{
		TArray<const FMetasoundFrontendEdge*> Edges;

		const FMetasoundFrontendGraph& Graph = GetDocument().RootGraph.Graph;
		const FMetasoundFrontendVertexHandle Handle { InNodeID, InVertexID };
		if (const int32* Index = InputToEdgeIndex.Find(Handle))
		{
			Edges.Add(&Graph.Edges[*Index]);
		}

		if (const TArray<int32>* Indices = OutputToEdgeIndices.Find(Handle))
		{
			Algo::Transform(*Indices, Edges, [&Graph](const int32& Index) { return &Graph.Edges[Index]; });
		}

		return Edges;
	}

	const int32* FDocumentGraphEdgeCache::FindEdgeIndexToNodeInput(const FGuid& InNodeID, const FGuid& InVertexID) const
	{
		return InputToEdgeIndex.Find({ InNodeID, InVertexID });
	}

	const TArray<int32>* FDocumentGraphEdgeCache::FindEdgeIndicesFromNodeOutput(const FGuid& InNodeID, const FGuid& InVertexID) const
	{
		return OutputToEdgeIndices.Find({ InNodeID, InVertexID });
	}

	const FMetasoundFrontendDocument& FDocumentGraphEdgeCache::GetDocument() const
	{
		check(Document);
		return *Document;
	}

	bool FDocumentGraphEdgeCache::IsNodeInputConnected(const FGuid& InNodeID, const FGuid& InVertexID) const
	{
		return InputToEdgeIndex.Contains(FMetasoundFrontendVertexHandle { InNodeID, InVertexID });
	}

	bool FDocumentGraphEdgeCache::IsNodeOutputConnected(const FGuid& InNodeID, const FGuid& InVertexID) const
	{
		return OutputToEdgeIndices.Contains(FMetasoundFrontendVertexHandle { InNodeID, InVertexID });
	}

	void FDocumentGraphEdgeCache::OnEdgeAdded(int32 InNewIndex)
	{
		const FMetasoundFrontendEdge& NewEdge = GetDocument().RootGraph.Graph.Edges[InNewIndex];
		InputToEdgeIndex.Add(FMetasoundFrontendVertexHandle { NewEdge.ToNodeID, NewEdge.ToVertexID }, InNewIndex);
		OutputToEdgeIndices.FindOrAdd({ NewEdge.FromNodeID, NewEdge.FromVertexID }).Add(InNewIndex);
	}

	void FDocumentGraphEdgeCache::OnRemovingEdge(int32 IndexToRemove)
	{
		const FMetasoundFrontendEdge& EdgeToRemove = GetDocument().RootGraph.Graph.Edges[IndexToRemove];
		InputToEdgeIndex.Remove(FMetasoundFrontendVertexHandle { EdgeToRemove.ToNodeID, EdgeToRemove.ToVertexID });
		TArray<int32>& Indices = OutputToEdgeIndices.FindChecked(EdgeToRemove.GetFromVertexHandle());
		Indices.RemoveAllSwap([&IndexToRemove](const int32& Index) { return Index == IndexToRemove; }, false /* bAllowShrinking */);
		if (Indices.IsEmpty())
		{
			OutputToEdgeIndices.Remove(FMetasoundFrontendVertexHandle { EdgeToRemove.FromNodeID, EdgeToRemove.FromVertexID });
		}
	}
} // namespace Metasound::Frontend
