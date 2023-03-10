// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "Graph/MovieGraphNode.h"

#include "MovieGraphConfig.generated.h"

// Forward Declare
class UMovieGraphNode;

USTRUCT(BlueprintType)
struct FMovieGraphBranch
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MovieGraph")
	FName BranchName;
};


USTRUCT(BlueprintType)
struct FMovieGraphTraversalContext
{
	GENERATED_BODY();

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MovieGraph")
	FMovieGraphBranch RootBranch;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MovieGraph")
	FString ShotName;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MovieGraph")
	FString RenderLayerName;
};

#if WITH_EDITOR
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnMovieGraphVariableChanged, class UMovieGraphMember*);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnMovieGraphInputChanged, class UMovieGraphMember*);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnMovieGraphOutputChanged, class UMovieGraphMember*);
#endif

UCLASS(Abstract)
class UMovieGraphMember : public UObject
{
	GENERATED_BODY()

public:
	UMovieGraphMember() = default;

	/** Gets the GUID that uniquely identifies this member. */
	const FGuid& GetGuid() const { return Guid; }

	/** Sets the GUID that uniquely identifies this member. */
	void SetGuid(const FGuid& InGuid) { Guid = InGuid; }

public:
	/** The type of data associated with this member. */
	UPROPERTY(EditAnywhere, Category = "General")
	EMovieGraphMemberType Type = EMovieGraphMemberType::Float;

	// TODO: Need a details customization that validates whether or not the name is valid/unique
	/** The name of this member, which is user-facing. */
	UPROPERTY(EditAnywhere, Category = "General")
	FString Name;

	/** The optional description of this member, which is user-facing. */
	UPROPERTY(EditAnywhere, Category = "General")
	FString Description;

	// TODO: This needs to eventually be capable of storing multiple types. Using TVariant looks promising, but it will
	// still require extensive details panel customizations
	/** The default value of this member. */
	UPROPERTY(EditAnywhere, Category = "General")
	float Default = 0.f;

private:
	/** A GUID that uniquely identifies this member within its graph. */
	UPROPERTY()
	FGuid Guid;
};

/**
 * A variable that can be created and used inside the graph. These variables can be controlled at
 * the job level.
 */
UCLASS(BlueprintType)
class UMovieGraphVariable : public UMovieGraphMember
{
	GENERATED_BODY()

public:
	UMovieGraphVariable() = default;

public:
#if WITH_EDITOR
	FOnMovieGraphVariableChanged OnMovieGraphVariableChangedDelegate;

	//~ Begin UObject overrides
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UObject overrides
#endif // WITH_EDITOR
};

/**
 * An input exposed on the graph that will be available for nodes to connect to.
 */
UCLASS(BlueprintType)
class UMovieGraphInput : public UMovieGraphMember
{
	GENERATED_BODY()

public:
	UMovieGraphInput() = default;

public:
#if WITH_EDITOR
	FOnMovieGraphInputChanged OnMovieGraphInputChangedDelegate;

	//~ Begin UObject overrides
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UObject overrides
#endif
};

/**
 * An output exposed on the graph that will be available for nodes to connect to.
 */
UCLASS(BlueprintType)
class UMovieGraphOutput : public UMovieGraphMember
{
	GENERATED_BODY()

public:
	UMovieGraphOutput() = default;

public:
#if WITH_EDITOR
	FOnMovieGraphOutputChanged OnMovieGraphOutputChangedDelegate;

	//~ Begin UObject overrides
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UObject overrides
#endif
};

#if WITH_EDITOR
	DECLARE_MULTICAST_DELEGATE(FOnMovieGraphChanged);
	DECLARE_MULTICAST_DELEGATE(FOnMovieGraphVariablesChanged);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnMovieGraphInputAdded, UMovieGraphInput*);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnMovieGraphOutputAdded, UMovieGraphOutput*);
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

	/** Adds a new variable member with default values to the graph. Returns the new variable on success, else nullptr. */
	UMovieGraphVariable* AddVariable();

	/** Adds a new input member to the graph. Returns the new input on success, else nullptr. */
	UMovieGraphInput* AddInput();

	/** Adds a new output member to the graph. Returns the new output on success, else nullptr. */
	UMovieGraphOutput* AddOutput();

	/** Gets the variable in the graph with the specified GUID, else nullptr if one could not be found. */
	UMovieGraphVariable* GetVariableByGuid(const FGuid& InGuid) const;

	/** Gets all variables that have been defined on the graph. */
	TArray<UMovieGraphVariable*> GetVariables() const;

	/** Gets all inputs that have been defined on the graph. */
	TArray<UMovieGraphInput*> GetInputs() const;

	/** Gets all outputs that have been defined on the graph. */
	TArray<UMovieGraphOutput*> GetOutputs() const;

	/** Remove the specified member (input, output, variable) from the graph. */
	void DeleteMember(UObject* MemberToDelete);

	/** Remove the specified variable member from the graph. */
	void DeleteVariableMember(UMovieGraphVariable* VariableMemberToDelete);

	/** Returns only the names of the root branches in the Output Node, with no depth information. */
	TArray<FMovieGraphBranch> GetOutputBranches() const;

	template<typename NodeType>
	NodeType* IterateGraphForClass(const FMovieGraphTraversalContext& InContext) const
	{
		TArray<NodeType*> AllSettings = IterateGraphForClassAll<NodeType>(InContext);
		if (AllSettings.Num() > 0)
		{
			return AllSettings[0];
		}
		return nullptr;
	}
	
	template<typename NodeType>
	TArray<NodeType*> IterateGraphForClassAll(const FMovieGraphTraversalContext& InContext) const
	{
		TArray<NodeType*> TypedNodes;
		const TArray<UMovieGraphNode*> FoundNodes = TraverseGraph(NodeType::StaticClass(), InContext);
		for (UMovieGraphNode* Node : FoundNodes)
		{
			TypedNodes.Add(CastChecked<NodeType>(Node));
		}

		return TypedNodes;
	}

	TArray<UMovieGraphNode*> TraverseGraph(TSubclassOf<UMovieGraphNode> InClassType, const FMovieGraphTraversalContext& InContext) const;

	/** Remove the specified input member from the graph. */
	void DeleteInputMember(UMovieGraphInput* InputMemberToDelete);

	/** Remove the specified output member from the graph. */
	void DeleteOutputMember(UMovieGraphOutput* OutputMemberToDelete);

protected:
	void TraverseGraphRecursive(UMovieGraphNode* InNode, TSubclassOf<UMovieGraphNode> InClassType, const FMovieGraphTraversalContext& InContext, TArray<UMovieGraphNode*>& OutNodes) const;

public:
#if WITH_EDITOR
	FOnMovieGraphChanged OnGraphChangedDelegate;
	FOnMovieGraphVariablesChanged OnGraphVariablesChangedDelegate;
	FOnMovieGraphInputAdded OnGraphInputAddedDelegate;
	FOnMovieGraphOutputAdded OnGraphOutputAddedDelegate;
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

private:
	/** Add a new member of type T to MemberArray, with a unique name that includes BaseName in it. */
	template<typename T>
	T* AddMember(TArray<TObjectPtr<T>>& MemberArray, const FText& BaseName);

private:
	/** All variables which have been defined on the graph. */
	UPROPERTY()
	TArray<TObjectPtr<UMovieGraphVariable>> Variables;

	/** All inputs which have been defined on the graph. */
	UPROPERTY()
	TArray<TObjectPtr<UMovieGraphInput>> Inputs;

	/** All outputs which have been defined on the graph. */
	UPROPERTY()
	TArray<TObjectPtr<UMovieGraphOutput>> Outputs;
};