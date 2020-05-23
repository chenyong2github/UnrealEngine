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

	const TArray<FInputDataVertexDescription>& FGraph::GetInputs() const
	{
		return InputDescriptions;
	}

	const TArray<FOutputDataVertexDescription>& FGraph::GetOutputs() const
	{
		return OutputDescriptions;
	}

	void FGraph::AddInputDataVertex(const FInputDataVertex& InVertex)
	{
		InputVertices.Add(InVertex);
		InputDescriptions.Add(InVertex.Description);
	}

	const TArray<FInputDataVertex>& FGraph::GetInputDataVertices() const
	{
		return InputVertices;
	}

	void FGraph::AddOutputDataVertex(const FOutputDataVertex& InVertex)
	{
		OutputVertices.Add(InVertex);
		OutputDescriptions.Add(InVertex.Description);
	}

	const TArray<FOutputDataVertex>& FGraph::GetOutputDataVertices() const
	{
		return OutputVertices;
	}

	void FGraph::AddDataEdge(const FDataEdge& InEdge)
	{
		Edges.Add(InEdge);
	}

	const TArray<FDataEdge>& FGraph::GetDataEdges() const
	{
		return Edges;
	}
}
