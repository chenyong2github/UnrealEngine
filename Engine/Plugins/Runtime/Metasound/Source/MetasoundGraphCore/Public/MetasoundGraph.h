// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundNode.h"

namespace Metasound
{
	// TODO: consider making nodes TSharedPtr<INode>
	/** FGraph contains the edges between nodes as well as input and output 
	 * vertices.  FGraph does not maintain ownership over any node. Nodes used
	 * within the graph must be valid for the lifetime of the graph. 
	 */
	class METASOUNDGRAPHCORE_API FGraph : public IGraph
	{
		public:
			static const FName ClassName;

			FGraph(const FString& InDescription);
			virtual ~FGraph();

			/** Return the name of this graph. */
			virtual const FString& GetDescription() const override;

			/** Return the type name of this graph. */
			virtual const FName& GetClassName() const override;

			/** Return a collection of input parameter descriptions for this graph. */
			virtual const FInputDataVertexCollection& GetInputDataVertices() const override;

			/** Return a collection of output parameter descriptions for this graph. */
			virtual const FOutputDataVertexCollection& GetOutputDataVertices() const override;

			/** Retrieve all the edges associated with a graph. */
			virtual const TArray<FDataEdge>& GetDataEdges() const override;

			/** Get vertices which contain input parameters. */
			virtual const FInputDataDestinationCollection& GetInputDataDestinations() const override;

			/** Get vertices which contain output parameters. */
			virtual const FOutputDataSourceCollection& GetOutputDataSources() const override;

			/** Add an edge to the graph. */
			void AddDataEdge(const FDataEdge& InEdge);

			/** Add an edge to the graph, connecting two vertices from two 
			 * nodes. 
			 *
			 * @param FromNode - Node which contains the output vertex.
			 * @param FromVertexName - Name of the vertex in the FromNode.
			 * @param ToNode - Node which contains the input vertex.
			 * @param ToVertexName - Name of the vertex in the ToNode.
			 *
			 * @return True if the edge was successfully added. False otherwise.
			 */
			bool AddDataEdge(INode& FromNode, const FString& FromVertexName, INode& ToNode, const FString ToVertexName);

			/** Add an input data destination to describe how data provided 
			 * outside this graph should be routed internally.
			 *
			 * @param InNode - Node which receives the data.
			 * @param InVertexName - Name of input vertex on InNode.
			 *
			 * @return True if the destination was successfully added. False 
			 * otherwise.
			 */
			bool AddInputDataDestination(INode& InNode, const FString& InVertexName);


			/** Add an input data destination to describe how data provided 
			 * outside this graph should be routed internally.
			 */
			void AddInputDataDestination(const FInputDataDestination& InDestination);

			/** Add an output data source which describes routing of data which is 
			 * owned this graph and exposed externally.
			 *
			 * @param InNode - Node which produces the data.
			 * @param InVertexName - Name of output vertex on InNode.
			 *
			 * @return True if the source was successfully added. False 
			 * otherwise.
			 */
			bool AddOutputDataSource(INode& InNode, const FString& InVertexName);

			/** Add an output data source which describes routing of data which is 
			 * owned this graph and exposed externally.
			 */
			void AddOutputDataSource(const FOutputDataSource& InSource);

			// TODO: Add ability to remove things.

		private:
			FString Description;

			FInputDataVertexCollection InputVertices;
			FOutputDataVertexCollection OutputVertices;

			TArray<FDataEdge> Edges;

			FInputDataDestinationCollection InputDestinations;
			FOutputDataSourceCollection OutputSources;
	};
}
