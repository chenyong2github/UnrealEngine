// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundGraph.h"

#include "Internationalization/Text.h"


namespace Metasound
{
	FGraph::FGraph(const FString& InInstanceName)
	: InstanceName(InInstanceName)
	, Metadata(FNodeInfo::GetEmpty())
	{
	}

	const FString& FGraph::GetInstanceName() const
	{
		return InstanceName;
	}

	const FNodeInfo& FGraph::GetMetadata() const
	{
		return Metadata;
	}

	bool FGraph::AddInputDataDestination(const INode& InNode, const FVertexKey& InVertexKey)
	{
		if (!InNode.GetVertexInterface().ContainsInputVertex(InVertexKey))
		{
			return false;
		}

		FInputDataDestination Destination(InNode, InNode.GetVertexInterface().GetInputVertex(InVertexKey));

		AddInputDataDestination(Destination);
		
		return true;
	}

	void FGraph::AddInputDataDestination(const FInputDataDestination& InDestination)
	{
		Metadata.DefaultInterface.GetInputInterface().Add(InDestination.Vertex);
		InputDestinations.Add(MakeDestinationDataVertexKey(InDestination), InDestination);
	}

	const FInputDataDestinationCollection& FGraph::GetInputDataDestinations() const
	{
		return InputDestinations;
	}

	bool FGraph::AddOutputDataSource(const INode& InNode, const FVertexKey& InVertexKey)
	{
		if (!InNode.GetVertexInterface().ContainsOutputVertex(InVertexKey))
		{
			return false;
		}

		FOutputDataSource Source(InNode, InNode.GetVertexInterface().GetOutputVertex(InVertexKey));

		AddOutputDataSource(Source);

		return true;
	}

	void FGraph::AddOutputDataSource(const FOutputDataSource& InSource)
	{
		Metadata.DefaultInterface.GetOutputInterface().Add(InSource.Vertex);
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

	bool FGraph::AddDataEdge(const INode& FromNode, const FVertexKey& FromKey, const INode& ToNode, const FVertexKey& ToKey)
	{
		const FVertexInterface& FromVertexInterface = FromNode.GetVertexInterface();
		const FVertexInterface& ToVertexInterface = ToNode.GetVertexInterface();

		if (!FromVertexInterface.ContainsOutputVertex(FromKey))
		{
			return false;
		}

		if (!ToVertexInterface.ContainsInputVertex(ToKey))
		{
			return false;
		}

		const FOutputDataVertex& FromVertex = FromVertexInterface.GetOutputVertex(FromKey);
		const FInputDataVertex& ToVertex = ToVertexInterface.GetInputVertex(ToKey);


		if (FromVertex.GetDataTypeName() != ToVertex.GetDataTypeName())
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

	const FVertexInterface& FGraph::GetVertexInterface() const 
	{
		return Metadata.DefaultInterface;
	}


	bool FGraph::SetVertexInterface(const FVertexInterface& InInterface)
	{
		return InInterface == Metadata.DefaultInterface;
	}

	bool FGraph::IsVertexInterfaceSupported(const FVertexInterface& InInterface) const
	{
		return InInterface == Metadata.DefaultInterface;
	}
}
