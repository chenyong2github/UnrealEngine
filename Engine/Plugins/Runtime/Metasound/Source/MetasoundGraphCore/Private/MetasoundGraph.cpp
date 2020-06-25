// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundGraph.h"

namespace Metasound
{
	const FName FGraph::ClassName = FName(TEXT("Graph"));

	FGraph::FGraph(const FString& InDescription)
	:	Description(InDescription)
	{
	}

	FGraph::~FGraph()
	{
	}

	const FString& FGraph::GetDescription() const
	{
		return Description;
	}

	const FName& FGraph::GetClassName() const
	{
		return ClassName;
	}

	const FInputDataVertexCollection& FGraph::GetInputDataVertices() const
	{
		return InputVertices;
	}

	const FOutputDataVertexCollection& FGraph::GetOutputDataVertices() const
	{
		return OutputVertices;
	}

	bool FGraph::AddInputDataDestination(INode& InNode, const FString& InVertexName)
	{
		if (!InNode.GetInputDataVertices().Contains(InVertexName))
		{
			return false;
		}

		FInputDataDestination Destination(InNode, InNode.GetInputDataVertices()[InVertexName]);

		AddInputDataDestination(Destination);

		return true;
	}

	void FGraph::AddInputDataDestination(const FInputDataDestination& InDestination)
	{
		InputVertices.Add(MakeDataVertexKey(InDestination.Vertex), InDestination.Vertex);
		InputDestinations.Add(MakeDestinationDataVertexKey(InDestination), InDestination);
	}

	const FInputDataDestinationCollection& FGraph::GetInputDataDestinations() const
	{
		return InputDestinations;
	}

	bool FGraph::AddOutputDataSource(INode& InNode, const FString& InVertexName)
	{
		if (!InNode.GetOutputDataVertices().Contains(InVertexName))
		{
			return false;
		}

		FOutputDataSource Source(InNode, InNode.GetOutputDataVertices()[InVertexName]);

		AddOutputDataSource(Source);

		return true;
	}

	void FGraph::AddOutputDataSource(const FOutputDataSource& InSource)
	{
		OutputVertices.Add(MakeDataVertexKey(InSource.Vertex), InSource.Vertex);
		OutputSources.Add(MakeSourceDataVertexKey(InSource), InSource);
	}

	const FOutputDataSourceCollection& FGraph::GetOutputDataSources() const
	{
		return OutputSources;
	}

	void FGraph::AddDataEdge(const FDataEdge& InEdge)
	{
		Edges.Add(InEdge);
	}

	// TODO: can we make node references const?  Would need to make operator factory accessing const as well.
	bool FGraph::AddDataEdge(INode& FromNode, const FString& FromName, INode& ToNode, const FString ToName)
	{
		
		if (!FromNode.GetOutputDataVertices().Contains(FromName))
		{
			return false;
		}

		if (!ToNode.GetInputDataVertices().Contains(ToName))
		{
			return false;
		}

		const FOutputDataVertex& FromVertex = FromNode.GetOutputDataVertices()[FromName];
		const FInputDataVertex& ToVertex = ToNode.GetInputDataVertices()[ToName];

		if (FromVertex.DataReferenceTypeName != ToVertex.DataReferenceTypeName)
		{
			return false;
		}

		FDataEdge Edge(FOutputDataSource(FromNode, FromVertex), FInputDataDestination(ToNode, ToVertex));

		AddDataEdge(Edge);

		return true;
	}

	const TArray<FDataEdge>& FGraph::GetDataEdges() const
	{
		return Edges;
	}
}
