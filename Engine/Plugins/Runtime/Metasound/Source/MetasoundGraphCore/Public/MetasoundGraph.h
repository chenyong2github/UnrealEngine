// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundNode.h"

namespace Metasound
{
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

			/** Return an array of input parameter descriptions for this graph. */
			virtual const TArray<FInputDataVertexDescription>& GetInputs() const override;

			/** Return an array of output parameter descriptions for this graph. */
			virtual const TArray<FOutputDataVertexDescription>& GetOutputs() const override;

			/** Retrieve all the edges associated with a graph. */
			virtual const TArray<FDataEdge>& GetDataEdges() const override;

			/** Get vertices which contain input parameters. */
			virtual const TArray<FInputDataVertex>& GetInputDataVertices() const override;

			/** Get vertices which contain output parameters. */
			virtual const TArray<FOutputDataVertex>& GetOutputDataVertices() const override;

			void AddDataEdge(const FDataEdge& InEdge);

			void AddInputDataVertex(const FInputDataVertex& InVertex);

			void AddOutputDataVertex(const FOutputDataVertex& InVertex);


			// TODO: Add ability to remove things.
			


		private:
			FString Description;

			TArray<FInputDataVertexDescription> InputDescriptions;
			TArray<FOutputDataVertexDescription> OutputDescriptions;

			TArray<FDataEdge> Edges;

			TArray<FInputDataVertex> InputVertices;
			TArray<FOutputDataVertex> OutputVertices;
	};
}
