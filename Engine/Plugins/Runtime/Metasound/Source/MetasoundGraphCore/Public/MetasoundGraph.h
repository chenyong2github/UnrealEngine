// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MetasoundNode.h"

// Forward Declarations
class FText;


namespace Metasound
{
	/** FGraph contains the edges between nodes as well as input and output 
	 * vertices.  FGraph does not maintain ownership over any node. Nodes used
	 * within the graph must be valid for the lifetime of the graph. 
	 */
	class METASOUNDGRAPHCORE_API FGraph : public IGraph
	{
		public:
			FGraph(const FString& InInstanceName);
			virtual ~FGraph() = default;

			/** Return the name of this specific instance of the node class. */
			const FString& GetInstanceName() const override;

			/** Return metadata about this graph. */
			const FNodeInfo& GetMetadata() const override;

			/** Retrieve all the edges associated with a graph. */
			const TArray<FDataEdge>& GetDataEdges() const override;

			/** Return the current vertex interface. */
			const FVertexInterface& GetVertexInterface() const override;

			/** Set the vertex interface. If the vertex was successfully changed, returns true. 
			 *
			 * @param InInterface - New interface for node. 
			 *
			 * @return True on success, false otherwise.
			 */
			bool SetVertexInterface(const FVertexInterface& InInterface) override;

			/** Expresses whether a specific vertex interface is supported.
			 *
			 * @param InInterface - New interface. 
			 *
			 * @return True if the interface is supported, false otherwise. 
			 */
			bool IsVertexInterfaceSupported(const FVertexInterface& InInterface) const override;

			/** Get vertices which contain input parameters. */
			const FInputDataDestinationCollection& GetInputDataDestinations() const override;

			/** Get vertices which contain output parameters. */
			const FOutputDataSourceCollection& GetOutputDataSources() const override;

			/** Add an edge to the graph. */
			void AddDataEdge(const FDataEdge& InEdge);

			/** Add an edge to the graph, connecting two vertices from two 
			 * nodes. 
			 *
			 * @param FromNode - Node which contains the output vertex.
			 * @param FromVertexKey - Key of the vertex in the FromNode.
			 * @param ToNode - Node which contains the input vertex.
			 * @param ToVertexKey - Key of the vertex in the ToNode.
			 *
			 * @return True if the edge was successfully added. False otherwise.
			 */
			bool AddDataEdge(const INode& FromNode, const FVertexKey& FromVertexKey, const INode& ToNode, const FVertexKey& ToVertexKey);

			/** Add an input data destination to describe how data provided 
			 * outside this graph should be routed internally.
			 *
			 * @param InNode - Node which receives the data.
			 * @param InVertexKey - Key for input vertex on InNode.
			 *
			 * @return True if the destination was successfully added. False 
			 * otherwise.
			 */
			bool AddInputDataDestination(const INode& InNode, const FVertexKey& InVertexKey);


			/** Add an input data destination to describe how data provided 
			 * outside this graph should be routed internally.
			 */
			void AddInputDataDestination(const FInputDataDestination& InDestination);

			/** Add an output data source which describes routing of data which is 
			 * owned this graph and exposed externally.
			 *
			 * @param InNode - Node which produces the data.
			 * @param InVertexKey - Key for output vertex on InNode.
			 *
			 * @return True if the source was successfully added. False 
			 * otherwise.
			 */
			bool AddOutputDataSource(const INode& InNode, const FVertexKey& InVertexKey);

			/** Add an output data source which describes routing of data which is 
			 * owned this graph and exposed externally.
			 */
			void AddOutputDataSource(const FOutputDataSource& InSource);

			// TODO: Add ability to remove things.

		private:
			FString InstanceName;
			FNodeInfo Metadata;


			TArray<FDataEdge> Edges;

			FInputDataDestinationCollection InputDestinations;
			FOutputDataSourceCollection OutputSources;
	};
}
