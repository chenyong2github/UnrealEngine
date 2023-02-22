// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "Graph/MovieGraphNode.h"
#include "MovieGraphConfig.generated.h"

// Forward Declare
class UMovieGraphNode;

USTRUCT(BlueprintType)
struct FMovieGraphTraversalContext
{
	GENERATED_BODY();

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Blah")
	FString ShotName;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Blah")
	FString RenderLayerName;
};

/**
* This is the runtime representation of the UMoviePipelineEdGraph which contains the actual strongly
* typed graph network that is read by the MoviePipeline. There is an editor-only representation of
* this graph (UMoviePipelineEdGraph).
*/
UCLASS()
class MOVIERENDERPIPELINECORE_API UMovieGraphConfig : public UObject
{
	GENERATED_BODY()

public:
	UMovieGraphConfig();
public:
	bool AddLabeledEdge(UMovieGraphNode* FromNode, const FName& FromPinLabel, UMovieGraphNode* ToNode, const FName& ToPinLabel);
	bool RemoveEdge(UMovieGraphNode* FromNode, const FName& FromPinName, UMovieGraphNode* ToNode, const FName& ToPinName);
	bool RemoveAllInboundEdges(UMovieGraphNode* InNode);
	bool RemoveAllOutboundEdges(UMovieGraphNode* InNode);
	bool RemoveInboundEdges(UMovieGraphNode* InNode, const FName& InPinName);
	bool RemoveOutboundEdges(UMovieGraphNode* InNode, const FName& InPinName);

	UMovieGraphNode* GetInputNode() const { return InputNode; }
	UMovieGraphNode* GetOutputNode() const { return OutputNode; }
	const TArray<TObjectPtr<UMovieGraphNode>>& GetNodes() const { return AllNodes; }

protected:
	UMovieGraphNode* FindTraversalStartForContext(const FMovieGraphTraversalContext& InContext) const;
public:
	UPROPERTY()
	TArray<TObjectPtr<UMovieGraphNode>> AllNodes;

	UPROPERTY()
	TObjectPtr<UMovieGraphNode> InputNode;

	UPROPERTY()
	TObjectPtr<UMovieGraphNode> OutputNode;

#if WITH_EDITORONLY_DATA
	// Not strongly typed to avoid a circular dependency between the editor only module
	// and the runtime module, but it should be a UMoviePipelineEdGraph.
	UPROPERTY(Transient)
	TObjectPtr<UEdGraph> PipelineEdGraph;
#endif

	template<class T>
	T* ConstructRuntimeNode(TSubclassOf<UMovieGraphNode> PipelineGraphNodeClass = T::StaticClass())
	{
		// Construct a new object with ourselves as the outer, then keep track of it.
		// ToDo: This is a runtime node, kept track in AllNodes, which is ultimately editor only. Probably
		// because the system it's based on (SoundCues) have a root node and links nodes together later?
		T* RuntimeNode = NewObject<T>(this, PipelineGraphNodeClass, NAME_None, RF_Transactional);
		RuntimeNode->UpdateDynamicProperties();
		RuntimeNode->UpdatePins();
#if WITH_EDITOR
		AllNodes.Add(RuntimeNode);
#endif // WITH_EDITORONLY_DATA
		return RuntimeNode;
	}

	void TraversalTest();
	void TraverseTest(UMovieGraphNode* InNode);

};