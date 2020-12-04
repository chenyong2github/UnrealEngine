// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "MetasoundFrontend.h"
#include "MetasoundFrontendDataLayout.h"
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
			 */
			FFrontendGraph(const FString& InInstanceName);

			virtual ~FFrontendGraph() = default;

			/** Add an input node to this graph.
			 *
			 * @param InNodeID - The NodeID related to the parent FMetasoundClassDescription.
			 * @param InIndex - The positional index for the input.
			 * @param InVertexKey - The key for the graph input vertex.
			 * @param InNode - A unique pointer to an input node. 
			 */
			void AddInputNode(int32 InNodeID, int32 InIndex, const FVertexKey& InVertexKey, TUniquePtr<INode> InNode);

			/** Add an output node to this graph.
			 *
			 * @param InNodeID - The NodeID related to the parent FMetasoundClassDescription.
			 * @param InIndex - The positional index for the output.
			 * @param InVertexKey - The key for the graph output vertex.
			 * @param InNode - A unique pointer to an output node. 
			 */
			void AddOutputNode(int32 InNodeID, int32 InIndex, const FVertexKey& InVertexKey, TUniquePtr<INode> InNode);

			/** Store a node on this graph. 
			 *
			 * @param InNodeID - The NodeID related to the parent FMetasoundClassDescription.
			 * @param InNode - A unique pointer to a node. 
			 */
			void AddNode(int32 InNodeID, TUniquePtr<INode> InNode);

			/** Retrieve node by dependency ID.
			 *
			 * @param InNodeID - The NodeID of the requested Node.
			 *
			 * @return Pointer to the Node if it is stored on this graph. nullptr otherwise. 
			 */
			const INode* FindNode(int32 InNodeID) const;

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

			TMap<int32, INode*> NodeMap;
			TSet<const INode*> StoredNodes;
			TArray<TUniquePtr<INode>> Storage;
	};

	/** FFrontendGraphBuilder builds a FFrontendGraph from a FMetasoundDoucment
	 * or FMetasoundClassDescription.
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
		bool IsFlat(const FMetasoundDocument& InDocument) const;

		/** Check that all dependencies are C++ class dependencies. 
		 * 
		 * @param InDependencies - Array of dependencies to check.
		 *
		 * @return True if all dependencies are C++ classes. False otherwise.
		 */
		bool IsFlat(const FMetasoundClassDescription& InRoot, const TArray<FMetasoundClassDescription>& InDependencies) const;
		
		/* Metasound document should be in order to create this graph. */
		TUniquePtr<FFrontendGraph> CreateGraph(const FMetasoundDocument& InDocument) const;

		/** Creates a graph given the root class and all of it's dependencies. */
		TUniquePtr<FFrontendGraph> CreateGraph(const FMetasoundClassDescription& InRootClass, const TArray<FMetasoundClassDescription>& InDependencies) const;

	private:

		TUniquePtr<INode> CreateInputNode(const FMetasoundNodeDescription& InNode, const FMetasoundInputDescription& InDescription) const;

		TUniquePtr<INode> CreateOutputNode(const FMetasoundNodeDescription& InNode, const FMetasoundOutputDescription& InDescription) const;

		TUniquePtr<INode> CreateExternalNode(const FMetasoundNodeDescription& InNode, const FMetasoundClassDescription& InClass) const;

		void SplitNodesByType(const TArray<FMetasoundNodeDescription>& InNodes, TArray<FMetasoundNodeDescription>& OutExternalNodes, TArray<FMetasoundNodeDescription>& OutInputNodes, TArray<FMetasoundNodeDescription>& OutOutputNodes) const;


		// TODO: add errors here. Most will be a "PromptIfMissing"...
		void AddExternalNodesToGraph(const TArray<FMetasoundNodeDescription>& InNodes, const TMap<int32, const FMetasoundClassDescription*>& InClasses, FFrontendGraph& OutGraph) const;

		void AddEdgesToGraph(const FMetasoundGraphDescription& InGraphDescription, FFrontendGraph& OutGraph) const;

		void AddInputDestinationsToGraph(const TArray<FMetasoundInputDescription>& InInputDescriptions, const TArray<FMetasoundNodeDescription>& InNodes, FFrontendGraph& OutGraph) const;
		void AddOutputSourcesToGraph(const TArray<FMetasoundOutputDescription>& InOutputDescriptions, const TArray<FMetasoundNodeDescription>& InNodes, FFrontendGraph& OutGraph) const;

	};
}
