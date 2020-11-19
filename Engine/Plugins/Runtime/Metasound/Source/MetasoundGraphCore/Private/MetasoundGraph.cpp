// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundGraph.h"

#include "Internationalization/Text.h"


namespace Metasound
{
	FGraph::FGraph(const FString& InDescription)
		: Description(InDescription)
	{
	}

	const FText& FGraph::GetDescription() const
	{
		return FText::GetEmpty();
	}

	const FString& FGraph::GetInstanceName() const
	{
		return Description;
	}

	const FName& FGraph::GetClassName() const
	{
		static const FName ClassName("Graph");
		return ClassName;
	}

	const FText& FGraph::GetAuthorName() const
	{
		return PluginAuthor;
	}

	const FText& FGraph::GetPromptIfMissing() const
	{
		return PluginNodeMissingPrompt;
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
		VertexInterface.GetInputInterface().Add(InDestination.Vertex);
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
		VertexInterface.GetOutputInterface().Add(InSource.Vertex);
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
		return VertexInterface;
	}

	const FVertexInterface& FGraph::GetDefaultVertexInterface() const 
	{
		static const FVertexInterface EmptyInterface;

		return EmptyInterface;
	}

	bool FGraph::SetVertexInterface(const FVertexInterface& InInterface)
	{
		return InInterface == VertexInterface;
	}

	bool FGraph::IsVertexInterfaceSupported(const FVertexInterface& InInterface) const
	{
		return InInterface == VertexInterface;
	}
}
