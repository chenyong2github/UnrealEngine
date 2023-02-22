// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "MovieEdGraph.generated.h"

class UMoviePipelineEdGraphNodeBase;
class UMovieGraphNode;
class UMovieGraphConfig;

/**
* This is the editor-only graph representation of the UMovieGraphConfig. This contains
* editor only nodes (which have information about their X/Y position graphs, their widgets, etc.)
* where each node in this ed graph is tied to a node in the runtime UMovieGraphConfig.
*/
UCLASS()
class UMoviePipelineEdGraph : public UEdGraph
{
	GENERATED_BODY()

public:

	/** Initialize this Editor Graph from a Runtime Graph */
	void InitFromRuntimeGraph(UMovieGraphConfig* InGraph);

	/** Returns the runtime UMovieGraphConfig that contains this editor graph */
	class UMovieGraphConfig* GetPipelineGraph() const;

	/** Creates the links/edges between nodes in the graph */
	void CreateLinks(UMoviePipelineEdGraphNodeBase* InGraphNode, bool bCreateInboundLinks, bool bCreateOutboundLinks);

protected:
	void CreateLinks(UMoviePipelineEdGraphNodeBase* InGraphNode, bool bCreateInboundLinks, bool bCreateOutboundLinks,
		const TMap<UMovieGraphNode*, UMoviePipelineEdGraphNodeBase*>& RuntimeNodeToEdNodeMap);

	bool bInitialized = false;
};