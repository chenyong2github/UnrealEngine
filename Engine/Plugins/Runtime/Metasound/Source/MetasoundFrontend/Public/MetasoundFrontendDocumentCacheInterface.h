// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/Array.h"
#include "HAL/Platform.h"
#include "UObject/Interface.h"


// Forward Declarations
struct FMetasoundFrontendClass;
struct FMetasoundFrontendClassMetadata;
struct FMetasoundFrontendDocument;
struct FMetaSoundFrontendDocumentBuilder;
struct FMetasoundFrontendEdge;
struct FMetasoundFrontendGraph;
struct FMetasoundFrontendNode;
struct FMetasoundFrontendVertex;


namespace Metasound::Frontend
{
	using FNodeRegistryKey = FString;

	/** Interface for querying cached document nodes. */
	class IDocumentGraphNodeCache
	{
	public:
		virtual ~IDocumentGraphNodeCache() = default;

		virtual bool ContainsNode(const FGuid& InNodeID) const = 0;
		virtual bool ContainsNodesOfClassID(const FGuid& InClassID) const = 0;

		virtual const int32* FindInputNodeIndex(FName InputName) const = 0;
		virtual const int32* FindOutputNodeIndex(FName OutputName) const = 0;
		virtual const int32* FindNodeIndex(const FGuid& InNodeID) const = 0;
		virtual TArray<const FMetasoundFrontendNode*> FindNodesOfClassID(const FGuid& InClassID) const = 0;

		virtual const FMetasoundFrontendNode* FindNode(const FGuid& InNodeID) const = 0;
		virtual const FMetasoundFrontendNode* FindInputNode(FName InputName) const = 0;
		virtual const FMetasoundFrontendNode* FindOutputNode(FName OutputName) const = 0;

		virtual TArray<const FMetasoundFrontendVertex*> FindNodeInputs(const FGuid& InNodeID, FName TypeName = FName()) const = 0;
		virtual TArray<const FMetasoundFrontendVertex*> FindNodeOutputs(const FGuid& InNodeID, FName TypeName = FName()) const = 0;

		virtual const FMetasoundFrontendVertex* FindInputVertex(const FGuid& InNodeID, const FGuid& InVertexID) const = 0;
		virtual const FMetasoundFrontendVertex* FindInputVertex(const FGuid& InNodeID, FName InVertexName) const = 0;
		virtual const FMetasoundFrontendVertex* FindOutputVertex(const FGuid& InNodeID, const FGuid& InVertexID) const = 0;
		virtual const FMetasoundFrontendVertex* FindOutputVertex(const FGuid& InNodeID, FName InVertexName) const = 0;

		virtual const FMetasoundFrontendDocument& GetDocument() const = 0;

	protected:
		virtual void OnNodeAdded(int32 NewItemIndex) = 0;
		virtual void OnRemovingNode(int32 ItemIndexBeingRemoved) = 0;

		friend struct ::FMetaSoundFrontendDocumentBuilder;
	};

	/** Interface for querying cached document edges. */
	class IDocumentGraphEdgeCache
	{
	public:
		virtual ~IDocumentGraphEdgeCache() = default;

		virtual bool ContainsEdge(const FMetasoundFrontendEdge& InEdge) const = 0;

		virtual bool IsNodeInputConnected(const FGuid& InNodeID, const FGuid& InVertexID) const = 0;
		virtual bool IsNodeOutputConnected(const FGuid& InNodeID, const FGuid& InVertexID) const = 0;

		virtual TArray<const FMetasoundFrontendEdge*> FindEdges(const FGuid& InNodeID, const FGuid& InVertexID) const = 0;
		virtual const int32* FindEdgeIndexToNodeInput(const FGuid& InNodeID, const FGuid& InVertexID) const = 0;
		virtual const TArray<int32>* FindEdgeIndicesFromNodeOutput(const FGuid& InNodeID, const FGuid& InVertexID) const = 0;

		virtual const FMetasoundFrontendDocument& GetDocument() const = 0;

	protected:
		virtual void OnEdgeAdded(int32 NewItemIndex) = 0;
		virtual void OnRemovingEdge(int32 ItemIndexBeingRemoved) = 0;

		friend struct ::FMetaSoundFrontendDocumentBuilder;
	};

	/** Interface for querying cached document dependencies. */
	class IDocumentCache
	{
	public:
		virtual ~IDocumentCache() = default;

		virtual bool ContainsDependency(const FNodeRegistryKey& InClassKey) const = 0;

		virtual const FMetasoundFrontendClass* FindDependency(const Metasound::Frontend::FNodeRegistryKey& InClassKey) const = 0;
		virtual const FMetasoundFrontendClass* FindDependency(const FGuid& InClassID) const = 0;
		virtual const int32* FindDependencyIndex(const Metasound::Frontend::FNodeRegistryKey& InClassKey) const = 0;
		virtual const int32* FindDependencyIndex(const FGuid& InClassID) const = 0;

		virtual IDocumentGraphNodeCache& GetNodeCache() = 0;
		virtual const IDocumentGraphNodeCache& GetNodeCache() const = 0;
		virtual IDocumentGraphEdgeCache& GetEdgeCache() = 0;
		virtual const IDocumentGraphEdgeCache& GetEdgeCache() const = 0;

		virtual const FMetasoundFrontendDocument& GetDocument() const = 0;

	protected:
		virtual void OnDependencyAdded(int32 NewItemIndex) = 0;
		virtual void OnRemovingDependency(int32 ItemIndexBeingRemoved) = 0;

		friend struct ::FMetaSoundFrontendDocumentBuilder;
	};

} // namespace Metasound::Frontend
