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
	}

	void FDocumentCache::Init(FDocumentModifyDelegates& OutDelegates)
	{
		METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(FDocumentCache::Init);

		check(Document);

		IDToIndex.Reset();
		KeyToIndex.Reset();

		for (int32 Index = 0; Index < Document->Dependencies.Num(); ++Index)
		{
			const FMetasoundFrontendClass& Class = Document->Dependencies[Index];
			const FMetasoundFrontendClassMetadata& Metadata = Class.Metadata;
			FNodeRegistryKey Key = NodeRegistryKey::CreateKey(Metadata);
			KeyToIndex.Add(MoveTemp(Key), Index);
			IDToIndex.Add(Class.ID, Index);
		}

		OutDelegates.OnDependencyAdded.AddSP(this, &FDocumentCache::OnDependencyAdded);
		OutDelegates.OnRemovingDependency.AddSP(this, &FDocumentCache::OnRemovingDependency);

		if (!EdgeCache.IsValid())
		{
			EdgeCache = MakeShared<FDocumentGraphEdgeCache>(AsShared());
		}
		EdgeCache->Init(OutDelegates.EdgeDelegates);

		if (!NodeCache.IsValid())
		{
			NodeCache = MakeShared<FDocumentGraphNodeCache>(AsShared());
		}
		NodeCache->Init(OutDelegates.NodeDelegates);

		if (!InterfaceCache.IsValid())
		{
			InterfaceCache = MakeShared<FDocumentGraphInterfaceCache>(AsShared());
		}
		InterfaceCache->Init(OutDelegates.InterfaceDelegates);
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

	const IDocumentGraphEdgeCache& FDocumentCache::GetEdgeCache() const
	{
		return *EdgeCache;
	}

	const IDocumentGraphNodeCache& FDocumentCache::GetNodeCache() const
	{
		return *NodeCache;
	}

	const IDocumentGraphInterfaceCache& FDocumentCache::GetInterfaceCache() const
	{
		return *InterfaceCache;
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

	FDocumentGraphInterfaceCache::FDocumentGraphInterfaceCache(TSharedRef<IDocumentCache> ParentCache)
		: Parent(ParentCache)
	{
	}

	const FMetasoundFrontendClassInput* FDocumentGraphInterfaceCache::FindInput(FName InputName) const
	{
		if (const int32* Index = InputNameToIndex.Find(InputName))
		{
			const FMetasoundFrontendDocument& Doc = Parent->GetDocument();
			return &Doc.RootGraph.Interface.Inputs[*Index];
		}

		return nullptr;
	}

	const FMetasoundFrontendClassOutput* FDocumentGraphInterfaceCache::FindOutput(FName OutputName) const
	{
		if (const int32* Index = OutputNameToIndex.Find(OutputName))
		{
			const FMetasoundFrontendDocument& Doc = Parent->GetDocument();
			return &Doc.RootGraph.Interface.Outputs[*Index];
		}

		return nullptr;
	}

	void FDocumentGraphInterfaceCache::Init(FInterfaceModifyDelegates& OutDelegates)
	{
		InputNameToIndex.Reset();
		OutputNameToIndex.Reset();

		const FMetasoundFrontendGraphClass& GraphClass = Parent->GetDocument().RootGraph;
		const TArray<FMetasoundFrontendClassInput>& Inputs = GraphClass.Interface.Inputs;
		for (int32 Index = 0; Index < Inputs.Num(); ++Index)
		{
			InputNameToIndex.Add(Inputs[Index].Name, Index);
		};

		const TArray<FMetasoundFrontendClassOutput>& Outputs = GraphClass.Interface.Outputs;
		for (int32 Index = 0; Index < Outputs.Num(); ++Index)
		{
			OutputNameToIndex.Add(Outputs[Index].Name, Index);
		};

		OutDelegates.OnInputAdded.AddSP(this, &FDocumentGraphInterfaceCache::OnInputAdded);
		OutDelegates.OnOutputAdded.AddSP(this, &FDocumentGraphInterfaceCache::OnOutputAdded);
		OutDelegates.OnRemovingInput.AddSP(this, &FDocumentGraphInterfaceCache::OnRemovingInput);
		OutDelegates.OnRemovingOutput.AddSP(this, &FDocumentGraphInterfaceCache::OnRemovingOutput);
	}

	void FDocumentGraphInterfaceCache::OnInputAdded(int32 NewIndex)
	{
		const FMetasoundFrontendDocument& Document = Parent->GetDocument();
		const FMetasoundFrontendGraphClass& GraphClass = Document.RootGraph;
		const FMetasoundFrontendClassInput& Input = GraphClass.Interface.Inputs[NewIndex];
		InputNameToIndex.Add(Input.Name, NewIndex);
	}

	void FDocumentGraphInterfaceCache::OnOutputAdded(int32 NewIndex)
	{
		const FMetasoundFrontendDocument& Document = Parent->GetDocument();
		const FMetasoundFrontendGraphClass& GraphClass = Document.RootGraph;
		const FMetasoundFrontendClassOutput& Output = GraphClass.Interface.Outputs[NewIndex];
		OutputNameToIndex.Add(Output.Name, NewIndex);
	}

	void FDocumentGraphInterfaceCache::OnRemovingInput(int32 IndexBeingRemoved)
	{
		const FMetasoundFrontendDocument& Document = Parent->GetDocument();
		const FMetasoundFrontendGraphClass& GraphClass = Document.RootGraph;
		const FMetasoundFrontendClassInput& Input = GraphClass.Interface.Inputs[IndexBeingRemoved];
		InputNameToIndex.Remove(Input.Name);
	}

	void FDocumentGraphInterfaceCache::OnRemovingOutput(int32 IndexBeingRemoved)
	{
		const FMetasoundFrontendDocument& Document = Parent->GetDocument();
		const FMetasoundFrontendGraphClass& GraphClass = Document.RootGraph;
		const FMetasoundFrontendClassOutput& Output = GraphClass.Interface.Outputs[IndexBeingRemoved];
		OutputNameToIndex.Remove(Output.Name);
	}

	FDocumentGraphNodeCache::FDocumentGraphNodeCache(TSharedRef<IDocumentCache> ParentCache)
		: Parent(ParentCache)
	{
	}

	void FDocumentGraphNodeCache::Init(FNodeModifyDelegates& OutDelegates)
	{
		IDToIndex.Reset();
		ClassIDToNodeIndices.Reset();

		const FMetasoundFrontendGraph& Graph = Parent->GetDocument().RootGraph.Graph;
		const TArray<FMetasoundFrontendNode>& Nodes = Graph.Nodes;
		for (int32 Index = 0; Index < Nodes.Num(); ++Index)
		{
			const FMetasoundFrontendNode& Node = Nodes[Index];
			const FMetasoundFrontendClass* Class = Parent->FindDependency(Node.ClassID);
			check(Class);

			const FGuid& NodeID = Node.GetID();
			IDToIndex.Add(NodeID, Index);
			ClassIDToNodeIndices.FindOrAdd(Node.ClassID).Add(Index);
		}

		OutDelegates.OnNodeAdded.AddSP(this, &FDocumentGraphNodeCache::OnNodeAdded);
		OutDelegates.OnRemovingNode.AddSP(this, &FDocumentGraphNodeCache::OnRemovingNode);
	}

	bool FDocumentGraphNodeCache::ContainsNode(const FGuid& InNodeID) const
	{
		return IDToIndex.Contains(InNodeID);
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
			const FMetasoundFrontendDocument& Document = Parent->GetDocument();
			const FMetasoundFrontendGraph& Graph = Document.RootGraph.Graph;
			Algo::Transform(*NodeIndices, Nodes, [&Graph](const int32& Index) { return &Graph.Nodes[Index]; });
		}
		return Nodes;
	}

	const FMetasoundFrontendNode* FDocumentGraphNodeCache::FindNode(const FGuid& InNodeID) const
	{
		if (const int32* NodeIndex = IDToIndex.Find(InNodeID))
		{
			const FMetasoundFrontendDocument& Document = Parent->GetDocument();
			const FMetasoundFrontendGraph& Graph = Document.RootGraph.Graph;
			return &Graph.Nodes[*NodeIndex];
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
		const FMetasoundFrontendDocument& Document = Parent->GetDocument();
		const FMetasoundFrontendGraph& Graph = Document.RootGraph.Graph;
		const FMetasoundFrontendNode& Node = Graph.Nodes[InNewIndex];
		IDToIndex.Add(Node.GetID(), InNewIndex);
		ClassIDToNodeIndices.FindOrAdd(Node.ClassID).Add(InNewIndex);
	}

	void FDocumentGraphNodeCache::OnRemovingNode(int32 IndexBeingRemoved)
	{
		using namespace Metasound::Frontend;

		// If not found, transaction was missed
		const FMetasoundFrontendDocument& Document = Parent->GetDocument();
		const FMetasoundFrontendNode& NodeBeingRemoved = Document.RootGraph.Graph.Nodes[IndexBeingRemoved];

		IDToIndex.Remove(NodeBeingRemoved.GetID());
		TArray<int32>& NodeIndices = ClassIDToNodeIndices.FindChecked(NodeBeingRemoved.ClassID);
		NodeIndices.Remove(IndexBeingRemoved);
		if (NodeIndices.IsEmpty())
		{
			ClassIDToNodeIndices.Remove(NodeBeingRemoved.ClassID);
		}
	}

	FDocumentGraphEdgeCache::FDocumentGraphEdgeCache(TSharedRef<IDocumentCache> ParentCache)
		: Parent(ParentCache)
	{
		const TArray<FMetasoundFrontendEdge>& Edges = Parent->GetDocument().RootGraph.Graph.Edges;
		for (int32 Index = 0; Index < Edges.Num(); ++Index)
		{
			const FMetasoundFrontendEdge& Edge = Edges[Index];
			OutputToEdgeIndices.FindOrAdd(Edge.GetFromVertexHandle()).Add(Index);
			InputToEdgeIndex.Add(Edge.GetToVertexHandle()) = Index;
		}
	}

	void FDocumentGraphEdgeCache::Init(FEdgeModifyDelegates& OutDelegates)
	{
		OutputToEdgeIndices.Reset();
		InputToEdgeIndex.Reset();

		OutDelegates.OnEdgeAdded.AddSP(this, &FDocumentGraphEdgeCache::OnEdgeAdded);
		OutDelegates.OnRemovingEdge.AddSP(this, &FDocumentGraphEdgeCache::OnRemovingEdge);
	}

	bool FDocumentGraphEdgeCache::ContainsEdge(const FMetasoundFrontendEdge& InEdge) const
	{
		const FMetasoundFrontendVertexHandle InputPair = { InEdge.ToNodeID, InEdge.ToVertexID };
		if (const int32* Index = InputToEdgeIndex.Find(InputPair))
		{
			const FMetasoundFrontendDocument& Document = Parent->GetDocument();
			const FMetasoundFrontendEdge& Edge = Document.RootGraph.Graph.Edges[*Index];
			return Edge.FromNodeID == InEdge.FromNodeID && Edge.FromVertexID == InEdge.FromVertexID;
		}

		return false;
	}

	TArray<const FMetasoundFrontendEdge*> FDocumentGraphEdgeCache::FindEdges(const FGuid& InNodeID, const FGuid& InVertexID) const
	{
		TArray<const FMetasoundFrontendEdge*> Edges;

		const FMetasoundFrontendDocument& Document = Parent->GetDocument();
		const FMetasoundFrontendGraph& Graph = Document.RootGraph.Graph;

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
		const FMetasoundFrontendDocument& Document = Parent->GetDocument();
		const FMetasoundFrontendGraph& Graph = Document.RootGraph.Graph;
		const FMetasoundFrontendEdge& NewEdge = Graph.Edges[InNewIndex];
		InputToEdgeIndex.Add(FMetasoundFrontendVertexHandle { NewEdge.ToNodeID, NewEdge.ToVertexID }, InNewIndex);
		OutputToEdgeIndices.FindOrAdd({ NewEdge.FromNodeID, NewEdge.FromVertexID }).Add(InNewIndex);
	}

	void FDocumentGraphEdgeCache::OnRemovingEdge(int32 IndexToRemove)
	{
		const FMetasoundFrontendDocument& Document = Parent->GetDocument();
		const FMetasoundFrontendGraph& Graph = Document.RootGraph.Graph;
		const FMetasoundFrontendEdge& EdgeToRemove = Graph.Edges[IndexToRemove];
		InputToEdgeIndex.Remove(FMetasoundFrontendVertexHandle { EdgeToRemove.ToNodeID, EdgeToRemove.ToVertexID });
		TArray<int32>& Indices = OutputToEdgeIndices.FindChecked(EdgeToRemove.GetFromVertexHandle());
		Indices.RemoveAllSwap([&IndexToRemove](const int32& Index) { return Index == IndexToRemove; }, false /* bAllowShrinking */);
		if (Indices.IsEmpty())
		{
			OutputToEdgeIndices.Remove(FMetasoundFrontendVertexHandle { EdgeToRemove.FromNodeID, EdgeToRemove.FromVertexID });
		}
	}
} // namespace Metasound::Frontend
