// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "MetasoundFrontend.h"
#include "MetasoundGraph.h"
#include "MetasoundNodeInterface.h"

namespace Metasound
{
	/** FFrontendGraph is a utility graph for use in the frontend. It can own nodes
	 * that live within the graph and provides query interfaces for finding nodes
	 * by dependency ID or input/output index. 
	 */
	class FFrontendGraph : public FGraph
	{
		// - TODO: consider adding ability to add in another sub graph
		public:
			/** FFrontendGraph constructor.
			 *
			 * @parma InInstanceName - Name of this graph.
			 * @parma InInstanceID - ID of this graph.
			 */
			FFrontendGraph(const FString& InInstanceName, const FGuid& InInstanceID);

			virtual ~FFrontendGraph() = default;

			/** Add an input node to this graph.
			 *
			 * @param InNodeID - The NodeID related to the parent FMetasoundFrontendClass.
			 * @param InIndex - The positional index for the input.
			 * @param InVertexKey - The key for the graph input vertex.
			 * @param InNode - A unique pointer to an input node. 
			 */
			void AddInputNode(FGuid InNodeID, int32 InIndex, const FVertexKey& InVertexKey, TUniquePtr<INode> InNode);

			/** Add an output node to this graph.
			 *
			 * @param InNodeID - The NodeID related to the parent FMetasoundFrontendClass.
			 * @param InIndex - The positional index for the output.
			 * @param InVertexKey - The key for the graph output vertex.
			 * @param InNode - A unique pointer to an output node. 
			 */
			void AddOutputNode(FGuid InNodeID, int32 InIndex, const FVertexKey& InVertexKey, TUniquePtr<INode> InNode);

			/** Store a node on this graph. 
			 *
			 * @param InNodeID - The NodeID related to the parent FMetasoundFrontendClass.
			 * @param InNode - A unique pointer to a node. 
			 */
			void AddNode(FGuid InNodeID, TUniquePtr<INode> InNode);

			/** Retrieve node by dependency ID.
			 *
			 * @param InNodeID - The NodeID of the requested Node.
			 *
			 * @return Pointer to the Node if it is stored on this graph. nullptr otherwise. 
			 */
			const INode* FindNode(FGuid InNodeID) const;

			/** Retrieve node by input index.
			 *
			 * @param InIndex - The index of the requested input.
			 *
			 * @return Pointer to the Node if it is stored on this graph. nullptr otherwise. 
			 */
			const INode* FindInputNode(int32 InIndex) const;

			/** Retrieve node by output index.
			 *
			 * @param InIndex - The index of the requested output.
			 *
			 * @return Pointer to the Node if it is stored on this graph. nullptr otherwise. 
			 */
			const INode* FindOutputNode(int32 InIndex) const;

			/** Returns true if all edges, destinations and sources refer to 
			 * nodes stored in this graph. */
			bool OwnsAllReferencedNodes() const;

		private:

			void StoreNode(TUniquePtr<INode> InNode);

			TMap<int32, INode*> InputNodes;
			TMap<int32, INode*> OutputNodes;

			TMap<FGuid, INode*> NodeMap;
			TSet<const INode*> StoredNodes;
			TArray<TUniquePtr<INode>> Storage;
	};

	/** FFrontendGraphBuilder builds a FFrontendGraph from a FMetasoundDoucment
	 * or FMetasoundFrontendClass.
	 */
	class FFrontendGraphBuilder
	{
	public:

		/** Check that all dependencies are C++ class dependencies. 
		 * 
		 * @param InDocument - Document containing dependencies.
		 *
		 * @return True if all dependencies are C++ classes. False otherwise.
		 */
		bool IsFlat(const FMetasoundFrontendDocument& InDocument) const;

		bool IsFlat(const FMetasoundFrontendGraphClass& InRoot, const TArray<FMetasoundFrontendClass>& InDependencies) const;

		/* Metasound document should be in order to create this graph. */
		TUniquePtr<FFrontendGraph> CreateGraph(const FMetasoundFrontendDocument& InDocument) const;

		/* Metasound document should be in order to create this graph. */
		TUniquePtr<FFrontendGraph> CreateGraph(const FMetasoundFrontendGraphClass& InGraph, const TArray<FMetasoundFrontendGraphClass>& InSubgraphs, const TArray<FMetasoundFrontendClass>& InDependencies) const;


	private:

		const FMetasoundFrontendClassInput* FindClassInputForInputNode(const FMetasoundFrontendGraphClass& InOwningGraph, const FMetasoundFrontendNode& InInputNode, int32& OutClassInputIndex) const;

		const FMetasoundFrontendClassOutput* FindClassOutputForOutputNode(const FMetasoundFrontendGraphClass& InOwningGraph, const FMetasoundFrontendNode& InOutputNode, int32& OutClassOutputIndex) const;

		const FMetasoundFrontendLiteral* FindInputLiteralForInputNode(const FMetasoundFrontendNode& InInputNode, const FMetasoundFrontendClass& InInputNodeClass, const FMetasoundFrontendClassInput& InOwningGraphClassInput) const;

		TUniquePtr<INode> CreateInputNode(const FMetasoundFrontendNode& InNode, const FMetasoundFrontendClass& InClass, const FMetasoundFrontendClassInput& InOwningGraphClassInput) const;

		TUniquePtr<INode> CreateOutputNode(const FMetasoundFrontendNode& InNode, const FMetasoundFrontendClass& InExternal) const;

		TUniquePtr<INode> CreateExternalNode(const FMetasoundFrontendNode& InNode, const FMetasoundFrontendClass& InClass) const;

		// TODO: add errors here. Most will be a "PromptIfMissing"...
		void AddNodesToGraph(const FMetasoundFrontendGraphClass& InGraphClass, const TMap<FGuid, const FMetasoundFrontendClass*>& InClasses, FFrontendGraph& OutGraph) const;

		void AddEdgesToGraph(const FMetasoundFrontendGraph& InGraphDescription, FFrontendGraph& OutGraph) const;
	};
}
