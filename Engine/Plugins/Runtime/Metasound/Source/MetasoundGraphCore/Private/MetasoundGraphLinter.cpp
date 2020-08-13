// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundGraphLinter.h"

#include "Algo/Count.h"
#include "Algo/Transform.h"
#include "CoreMinimal.h"

#include "MetasoundBuildError.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundGraphAlgo.h"

namespace Metasound
{
	namespace GraphLinterPrivate
	{
		using FBuildErrorPtr = TUniquePtr<IOperatorBuildError>;

		template<typename ErrorType, typename... ArgTypes>
		void AddBuildError(TArray<FBuildErrorPtr>& OutErrors, ArgTypes&&... Args)
		{
			OutErrors.Add(MakeUnique<ErrorType>(Forward<ArgTypes>(Args)...));
		}

		// run validation on input destination
		bool IsInputDestinationValid(const FInputDataDestination& InDest)
		{
			if (InDest.Node == nullptr)
			{
				return false;
			}

			const FInputDataVertexCollection& InputCollection = InDest.Node->GetInputDataVertices();

			FDataVertexKey VertexKey = MakeDataVertexKey(InDest.Vertex);

			if (!InputCollection.Contains(VertexKey))
			{
				return false;
			}

			if (InputCollection[VertexKey] != InDest.Vertex)
			{
				return false;
			}

			return true;
		}
		
		// run validation on output source
		bool IsOutputSourceValid(const FOutputDataSource& InSource)
		{
			if (InSource.Node == nullptr)
			{
				return false;
			}

			const FOutputDataVertexCollection& OutputCollection = InSource.Node->GetOutputDataVertices();

			FDataVertexKey VertexKey = MakeDataVertexKey(InSource.Vertex);

			if (!OutputCollection.Contains(VertexKey))
			{
				return false;
			}

			if (OutputCollection[VertexKey] != InSource.Vertex)
			{
				return false;
			}

			return true;
		};

		const FDataEdge* GetEdgePointer(const FDataEdge& InEdge) 
		{ 
			return &InEdge; 
		};

		// Raw pointer comparison for sorting.
		bool CompareEdgeDestinationNode(const FDataEdge& InLHS, const FDataEdge& InRHS)
		{
			return InLHS.To.Node < InRHS.To.Node;
		}
	}

	bool FGraphLinter::ValidateEdgeDataTypesMatch(const IGraph& InGraph, TArray<FBuildErrorPtr>& OutErrors)
	{
		using namespace GraphLinterPrivate;

		bool bIsValid = true;

		for (const FDataEdge& Edge : InGraph.GetDataEdges())
		{
			if (Edge.From.Vertex.DataReferenceTypeName != Edge.To.Vertex.DataReferenceTypeName)
			{
				AddBuildError<FInvalidConnectionDataTypeError>(OutErrors, Edge);

				bIsValid = false;
			}
		}

		return bIsValid;
	}

	bool FGraphLinter::ValidateVerticesExist(const IGraph& InGraph, TArray<FBuildErrorPtr>& OutErrors) 
	{
		using namespace GraphLinterPrivate;

		bool bAllVerticesAreValid = true;

		for (const FDataEdge& Edge : InGraph.GetDataEdges())
		{
			if (nullptr == Edge.From.Node)
			{
				bAllVerticesAreValid = false;
				AddBuildError<FDanglingVertexError>(OutErrors, Edge.From);
			}

			if (!IsOutputSourceValid(Edge.From))
			{
				bAllVerticesAreValid = false;
				AddBuildError<FMissingVertexError>(OutErrors, Edge.From);
			}

			if (nullptr == Edge.To.Node)
			{
				bAllVerticesAreValid = false;
				AddBuildError<FDanglingVertexError>(OutErrors, Edge.To);
			}

			if (!IsInputDestinationValid(Edge.To))
			{
				bAllVerticesAreValid = false;
				AddBuildError<FMissingVertexError>(OutErrors, Edge.To);
			}
		}

		for (auto Element : InGraph.GetInputDataDestinations())
		{
			const FInputDataDestination& Dest = Element.Value;
			if (!IsInputDestinationValid(Dest))
			{
				bAllVerticesAreValid = false;
				AddBuildError<FMissingVertexError>(OutErrors, Dest);
			}
		}

		for (auto Element : InGraph.GetOutputDataSources())
		{
			const FOutputDataSource& Source = Element.Value;
			if (!IsOutputSourceValid(Source))
			{
				bAllVerticesAreValid = false;
				AddBuildError<FMissingVertexError>(OutErrors, Source);
			}
		}
		
		return bAllVerticesAreValid;
	}

	bool FGraphLinter::ValidateNoDuplicateInputs(const IGraph& InGraph, TArray<FBuildErrorPtr>& OutErrors)
	{
		using namespace GraphLinterPrivate;

		bool bIsValid = true;

		TArray<const FDataEdge*> Edges;

		Algo::Transform(InGraph.GetDataEdges(), Edges, GetEdgePointer);

		Edges.Sort(CompareEdgeDestinationNode);

		if (Edges.Num() < 1)
		{
			return bIsValid;
		}

		int32 GroupStartIndex = 0;
		int32 GroupEndIndex = 0;
		const INode* CurrentNode = Edges[GroupStartIndex]->To.Node;

		for (int32 i = 1; i < Edges.Num(); i++)
		{
			const INode* Node = Edges[i]->To.Node;
			if (Node == CurrentNode)
			{
				GroupEndIndex = i;
			}
			else
			{
				if (GroupEndIndex > GroupStartIndex)
				{
					bIsValid = false;

					TArray<FDataEdge> Duplicates;
					for (int32 j = GroupStartIndex; j <= GroupEndIndex; j++)
					{
						Duplicates.Add(*Edges[j]);
						AddBuildError<FDuplicateInputError>(OutErrors, Duplicates);
					}
				}

				GroupStartIndex = GroupEndIndex = i;
				CurrentNode = Node;
			}
		}

		return bIsValid;
	}

	bool FGraphLinter::ValidateNoCyclesInGraph(const IGraph& InGraph, TArray<FBuildErrorPtr>& OutErrors) 
	{
		TPimplPtr<FDirectedGraphAlgoAdapter> Adapter = FDirectedGraphAlgo::CreateDirectedGraphAlgoAdapter(InGraph);

		check(Adapter.IsValid());

		return ValidateNoCyclesInGraph(*Adapter, OutErrors);
	}

	bool FGraphLinter::ValidateNoCyclesInGraph(const FDirectedGraphAlgoAdapter& InAdapter, TArray<FBuildErrorPtr>& OutErrors) 
	{
		using namespace GraphLinterPrivate;

		bool bIsValid = true;

		TArray<FStronglyConnectedComponent> Cycles;

		// In graph theory, a single vertex is technically a strongly connected
		// component. The graph linter is only interested in strongly connected
		// components of more than one vertex since this denotes a cycle.
		bool bExcludeSingleVertex = true;

		if(FDirectedGraphAlgo::TarjanStronglyConnectedComponents(InAdapter, Cycles, bExcludeSingleVertex))
		{
			bIsValid = false;

			for (const FStronglyConnectedComponent& Cycle : Cycles)
			{
				AddBuildError<FGraphCycleError>(OutErrors, Cycle.Nodes, Cycle.Edges);
			}
		}

		return bIsValid;
	}
}
