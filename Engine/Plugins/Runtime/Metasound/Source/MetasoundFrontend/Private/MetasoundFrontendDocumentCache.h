// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "HAL/Platform.h"
#include "MetasoundDocumentInterface.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendDocumentCacheInterface.h"
#include "Containers/SortedMap.h"

// Forward Declarations
struct FMetaSoundFrontendDocumentBuilder;


namespace Metasound::Frontend
{
	// Forward Declarations
	class FDocumentCache;

	class FDocumentGraphEdgeCache : public IDocumentGraphEdgeCache
	{
	public:
		FDocumentGraphEdgeCache() = default;
		FDocumentGraphEdgeCache(const FMetasoundFrontendDocument& InDocument);
		virtual ~FDocumentGraphEdgeCache() = default;

		// IDocumentGraphEdgeCache implementation
		virtual bool ContainsEdge(const FMetasoundFrontendEdge& InEdge) const override;

		virtual bool IsNodeInputConnected(const FGuid& InNodeID, const FGuid& InVertexID) const override;
		virtual bool IsNodeOutputConnected(const FGuid& InNodeID, const FGuid& InVertexID) const override;

		virtual TArray<const FMetasoundFrontendEdge*> FindEdges(const FGuid& InNodeID, const FGuid& InVertexID) const override;
		virtual const int32* FindEdgeIndexToNodeInput(const FGuid& InNodeID, const FGuid& InVertexID) const override;
		virtual const TArray<int32>* FindEdgeIndicesFromNodeOutput(const FGuid& InNodeID, const FGuid& InVertexID) const override;

	protected:
		virtual void OnEdgeAdded(int32 InNewIndex) override;
		virtual void OnRemovingEdge(int32 IndexBeingRemoved) override;
		virtual const FMetasoundFrontendDocument& GetDocument() const override;

		// Friended to ensure only builder is the one managing cache manipulation via add/remove calls above
		friend struct ::FMetaSoundFrontendDocumentBuilder;

	private:

		// Cache of outputs NodeId/VertexId pairs to associated edge indices
		TMap<FMetasoundFrontendVertexHandle, TArray<int32>> OutputToEdgeIndices;

		// Cache of Input NodeId/VertexId pairs to associated edge indices
		TMap<FMetasoundFrontendVertexHandle, int32> InputToEdgeIndex;

		const FMetasoundFrontendDocument* Document = nullptr;
	};

	class FDocumentGraphNodeCache : public IDocumentGraphNodeCache
	{
	public:
		FDocumentGraphNodeCache() = default;
		FDocumentGraphNodeCache(const FMetasoundFrontendDocument& InDocument, const FDocumentCache& InParent);
		virtual ~FDocumentGraphNodeCache() = default;

		// IDocumentGraphNodeCache implementation
		virtual bool ContainsNode(const FGuid& InNodeID) const override;
		virtual bool ContainsNodesOfClassID(const FGuid& InClassID) const override;

		virtual const int32* FindInputNodeIndex(FName InputName) const override;
		virtual const int32* FindOutputNodeIndex(FName OutputName) const override;
		virtual const int32* FindNodeIndex(const FGuid& InNodeID) const override;
		virtual TArray<const FMetasoundFrontendNode*> FindNodesOfClassID(const FGuid& InClassID) const override;
		virtual const FMetasoundFrontendNode* FindNode(const FGuid& InNodeID) const override;
		virtual const FMetasoundFrontendNode* FindInputNode(FName InputName) const override;
		virtual const FMetasoundFrontendNode* FindOutputNode(FName OutputName) const override;

		virtual TArray<const FMetasoundFrontendVertex*> FindNodeInputs(const FGuid& InNodeID, FName TypeName = FName()) const override;
		virtual TArray<const FMetasoundFrontendVertex*> FindNodeOutputs(const FGuid& InNodeID, FName TypeName = FName()) const override;

		virtual const FMetasoundFrontendVertex* FindInputVertex(const FGuid& InNodeID, const FGuid& InVertexID) const override;
		virtual const FMetasoundFrontendVertex* FindInputVertex(const FGuid& InNodeID, FName InVertexName) const override;
		virtual const FMetasoundFrontendVertex* FindOutputVertex(const FGuid& InNodeID, const FGuid& InVertexID) const override;
		virtual const FMetasoundFrontendVertex* FindOutputVertex(const FGuid& InNodeID, FName InVertexName) const override;

	protected:
		virtual void OnNodeAdded(int32 NewIndex) override;
		virtual void OnRemovingNode(int32 IndexBeingRemoved) override;
		virtual const FMetasoundFrontendDocument& GetDocument() const override;

		// Friended to ensure only builder is the one managing cache manipulation via add/remove calls above
		friend struct ::FMetaSoundFrontendDocumentBuilder;

	private:

		// Cache of Input name to array index of node
		TMap<FName, int32> InputNameToIndex;

		// Cache of Output name to array index of node
		TMap<FName, int32> OutputNameToIndex;

		// Cache of NodeId to array index of node
		TSortedMap<FGuid, int32> IDToIndex;

		// Cache of ClassID to referencing node indices
		TSortedMap<FGuid, TArray<int32>> ClassIDToNodeIndices;

		const FMetasoundFrontendDocument* Document = nullptr;
		const FDocumentCache* Parent = nullptr;
	};

	class FDocumentCache : public IDocumentCache
	{
	public:
		FDocumentCache() = default;
		FDocumentCache(const FMetasoundFrontendDocument& InDocument);
		virtual ~FDocumentCache() = default;

		virtual bool ContainsDependency(const FNodeRegistryKey& InClassKey) const override;

		virtual const FMetasoundFrontendClass* FindDependency(const FNodeRegistryKey& InClassKey) const override;
		virtual const FMetasoundFrontendClass* FindDependency(const FGuid& InClassID) const override;
		virtual const int32* FindDependencyIndex(const Metasound::Frontend::FNodeRegistryKey& InClassKey) const override;
		virtual const int32* FindDependencyIndex(const FGuid& InClassID) const override;


		virtual IDocumentGraphEdgeCache& GetEdgeCache() override { return *EdgeCache; }
		virtual const IDocumentGraphEdgeCache& GetEdgeCache() const override { return *EdgeCache; }
		virtual IDocumentGraphNodeCache& GetNodeCache() override { return *NodeCache; }
		virtual const IDocumentGraphNodeCache& GetNodeCache() const override { return *NodeCache; }

	protected:
		virtual void OnDependencyAdded(int32 InNewIndex) override;
		virtual void OnRemovingDependency(int32 IndexBeingRemoved) override;
		virtual const FMetasoundFrontendDocument& GetDocument() const override;

	private:

		// Cache of dependency (Class) ID to corresponding class dependency index
		TSortedMap<FGuid, int32> IDToIndex;

		// Cache of version data to corresponding class dependency index
		TSortedMap<FNodeRegistryKey, int32> KeyToIndex;

		TUniquePtr<FDocumentGraphEdgeCache> EdgeCache;
		TUniquePtr<FDocumentGraphNodeCache> NodeCache;

		const FMetasoundFrontendDocument* Document = nullptr;

		friend struct ::FMetaSoundFrontendDocumentBuilder;
	};
} // namespace Metasound::Frontend
