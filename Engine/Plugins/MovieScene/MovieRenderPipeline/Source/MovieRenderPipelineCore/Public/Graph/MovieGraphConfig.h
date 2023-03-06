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

/** Types of UMovieGraphVariable that can be used in the graph. */
UENUM()
enum class EMovieGraphVariableType : uint8
{
	Bool,
	Float,
	Int,
	String
};

#if WITH_EDITOR
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnMovieGraphVariableChanged, UMovieGraphVariable*);
#endif 

/**
 * A variable that can be created and used inside the graph. These variables can be controlled at
 * the job level.
 */
UCLASS(BlueprintType)
class UMovieGraphVariable : public UObject
{
	GENERATED_BODY()

public:
	UMovieGraphVariable() = default;

	/** Gets the GUID that uniquely identifies this variable. */
	const FGuid& GetGuid() const { return Guid; }

	/** Sets the GUID that uniquely identifies this variable. */
	void SetGuid(const FGuid& InGuid) { Guid = InGuid; }

public:
#if WITH_EDITOR
	FOnMovieGraphVariableChanged OnMovieGraphVariableChangedDelegate;
#endif

	/** The type of data stored in this variable. */
	UPROPERTY(EditAnywhere, Category = "General")
	EMovieGraphVariableType Type = EMovieGraphVariableType::Float;

	// TODO: Need a details customization that validates whether or not the name is valid/unique
	/** The name of this variable, which is user-facing. */
	UPROPERTY(EditAnywhere, Category = "General")
	FString Name;

	/** The optional description of this variable, which is user-facing. */
	UPROPERTY(EditAnywhere, Category = "General")
	FString Description;

	// TODO: This needs to eventually be capable of storing multiple types. Using TVariant looks promising, but it will
	// still require extensive details panel customizations
	/** The default value of this variable. */
	UPROPERTY(EditAnywhere, Category = "General")
	float Default = 0.f;

	//~ Begin UObject overrides
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End UObject overrides

private:
	/** A GUID that uniquely identifies this variable within its graph. */
	UPROPERTY()
	FGuid Guid;
};

#if WITH_EDITOR
	DECLARE_MULTICAST_DELEGATE(FOnMovieGraphChanged);
	DECLARE_MULTICAST_DELEGATE(FOnMovieGraphVariablesChanged);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnMovieGraphNodesDeleted, TArray<UMovieGraphNode*>);
#endif // WITH_EDITOR

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

	bool AddLabeledEdge(UMovieGraphNode* FromNode, const FName& FromPinLabel, UMovieGraphNode* ToNode, const FName& ToPinLabel);
	bool RemoveEdge(UMovieGraphNode* FromNode, const FName& FromPinName, UMovieGraphNode* ToNode, const FName& ToPinName);
	bool RemoveAllInboundEdges(UMovieGraphNode* InNode);
	bool RemoveAllOutboundEdges(UMovieGraphNode* InNode);
	bool RemoveInboundEdges(UMovieGraphNode* InNode, const FName& InPinName);
	bool RemoveOutboundEdges(UMovieGraphNode* InNode, const FName& InPinName);

	/** Removes the specified node from the graph. */
	bool RemoveNode(UMovieGraphNode* InNode);
	bool RemoveNodes(TArray<UMovieGraphNode*> InNodes);


	UMovieGraphNode* GetInputNode() const { return InputNode; }
	UMovieGraphNode* GetOutputNode() const { return OutputNode; }
	const TArray<TObjectPtr<UMovieGraphNode>>& GetNodes() const { return AllNodes; }

	/** Adds a new variable with default values to the graph. Returns the new variable on success, else nullptr. */
	UMovieGraphVariable* AddVariable();

	/** Gets the variable in the graph with the specified GUID, else nullptr if one could not be found. */
	UMovieGraphVariable* GetVariableByGuid(const FGuid& InGuid) const;

	/** Get all variables that have been defined on the graph. */
	TArray<UMovieGraphVariable*> GetVariables() const;

	/** Remove the specified member (input, output, variable) from the graph. */
	void DeleteMember(UObject* MemberToDelete);

	/** Remove the specified variable member from the graph. */
	void DeleteVariableMember(UMovieGraphVariable* VariableMemberToDelete);

protected:
	UMovieGraphNode* FindTraversalStartForContext(const FMovieGraphTraversalContext& InContext) const;

public:
#if WITH_EDITOR
	FOnMovieGraphChanged OnGraphChangedDelegate;
	FOnMovieGraphVariablesChanged OnGraphVariablesChangedDelegate;
	FOnMovieGraphNodesDeleted OnGraphNodesDeletedDelegate;
#endif
	
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
		RuntimeNode->Guid = FGuid::NewGuid();
#if WITH_EDITOR
		AllNodes.Add(RuntimeNode);
#endif
		return RuntimeNode;
	}

	void TraversalTest();
	void TraverseTest(UMovieGraphNode* InNode);

private:
	void OnVariableUpdated(UMovieGraphVariable* UpdatedVariable);

private:
	/** All variables which have been defined on the graph. */
	UPROPERTY()
	TArray<TObjectPtr<UMovieGraphVariable>> Variables;
};