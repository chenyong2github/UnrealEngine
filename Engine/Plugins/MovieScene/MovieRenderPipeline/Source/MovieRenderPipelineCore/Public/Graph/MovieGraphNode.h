// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MovieGraphPin.h"
#include "InstancedStruct.h"
#include "PropertyBag.h"
#include "Graph/MovieGraphValueContainer.h"

#if WITH_EDITOR
#include "Textures/SlateIcon.h"
#include "Math/Color.h"
#endif

#include "MovieGraphNode.generated.h"

// Forward Declares
class UMovieGraphInput;
class UMovieGraphMember;
class UMovieGraphOutput;
class UMovieGraphPin;
class UMovieGraphVariable;
struct FMovieGraphEvaluationContext;

#if WITH_EDITOR
class UEdGraphNode;
#endif

DECLARE_MULTICAST_DELEGATE_OneParam(FOnMovieGraphNodeChanged, const UMovieGraphNode*);

/**
* This is a base class for all nodes that can exist in the UMovieGraphConfig network.
* In the editor, each node in the network will have an editor-only representation too 
* which contains data about it's visual position in the graph, comments, etc.
*/
UCLASS(Abstract)
class MOVIERENDERPIPELINECORE_API UMovieGraphNode : public UObject
{
	GENERATED_BODY()


	friend class UMovieGraphConfig;
	friend class UMovieGraphEdge;
	
public:
	static FName GlobalsPinName;
	UMovieGraphNode();

	const TArray<TObjectPtr<UMovieGraphPin>>& GetInputPins() const { return InputPins; }
	const TArray<TObjectPtr<UMovieGraphPin>>& GetOutputPins() const { return OutputPins; }
	
	virtual TArray<FMovieGraphPinProperties> GetInputPinProperties() const
	{
		return TArray<FMovieGraphPinProperties>();
	}
	virtual TArray<FMovieGraphPinProperties> GetOutputPinProperties() const
	{
		return TArray<FMovieGraphPinProperties>();
	}

	virtual TArray<FPropertyBagPropertyDesc> GetDynamicPropertyDescriptions() const
	{
		return TArray<FPropertyBagPropertyDesc>();
	}

	virtual TArray<FName> GetExposedDynamicProperties() const
	{ 
		return ExposedDynamicPropertyNames;
	}

	/** 
	* Used to determine which Branch type pins we should follow when trying to traverse the graph.
	* By default we will follow any input pin (with Branch type) on the node, but override this in
	* inherited classes and change that if you need custom logic, such as boolean nodes that want 
	* to choose one or the other based on the results of a conditional property.
	*/
	virtual TArray<UMovieGraphPin*> EvaluatePinsToFollow(FMovieGraphEvaluationContext& InContext) const;

	/**
	* When a non-branch pin type is being evaluated on a node, the calling node will ask this node
	* for the value connected to the given pin name. For example, a Branch node will call this
	* function on whatever node is connected to the Conditional pin, and then will try to get a 
	* Boolean value out of the returned UMovieGraphValueContainer.
	*/
	virtual UMovieGraphValueContainer* GetPropertyValueContainerForPin(const FString& InPinName) const
	{
		return nullptr;
	}

	/** Promotes the property with the given name to a pin on the node via a dynamic property. */
	virtual void PromoteDynamicPropertyToPin(const FName& PropertyName);

	void UpdatePins();
	void UpdateDynamicProperties();
	class UMovieGraphConfig* GetGraph() const;
	UMovieGraphPin* GetInputPin(const FName& InPinLabel) const;
	UMovieGraphPin* GetOutputPin(const FName& InPinLabel) const;

	/** Gets the GUID which uniquely identifies this node. */
	const FGuid& GetGuid() const { return Guid; }

#if WITH_EDITOR
	int32 GetNodePosX() const { return NodePosX; }
	int32 GetNodePosY() const { return NodePosY; }

	void SetNodePosX(const int32 InNodePosX) { NodePosX = InNodePosX; }
	void SetNodePosY(const int32 InNodePosY) { NodePosY = InNodePosY; }

	/** Gets the node's title color, as visible in the graph. */
	virtual FLinearColor GetNodeTitleColor() const;

	/** Gets the node's icon and icon tint, as visible in the graph. */
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const;

	FString GetNodeComment() const { return NodeComment; }
	void SetNodeComment(const FString& InNodeComment) { NodeComment = InNodeComment; }

	bool IsCommentBubblePinned() const { return bIsCommentBubblePinned; }
	void SetIsCommentBubblePinned(const uint8 bIsPinned) { bIsCommentBubblePinned = bIsPinned; }

	bool IsCommentBubbleVisible() const { return bIsCommentBubbleVisible; }
	void SetIsCommentBubbleVisible(uint8 bIsVisible) { bIsCommentBubbleVisible = bIsVisible; }
#endif

	//~ Begin UObject Interface
	virtual void PostLoad() override;
	//~ End UObject Interface

public:
	FOnMovieGraphNodeChanged OnNodeChangedDelegate;

#if WITH_EDITORONLY_DATA
	/** Editor Node Graph representation. Not strongly typed to avoid circular dependency between editor/runtime modules. */
	UPROPERTY()
	TObjectPtr<UEdGraphNode>	GraphNode;

	class UEdGraphNode* GetGraphNode() const;
#endif

#if WITH_EDITOR
	/**
	 * Gets the node's title. Optionally gets a more descriptive, multi-line title for the node if bGetDescriptive is
	 * set to true.
	 */
	virtual FText GetNodeTitle(const bool bGetDescriptive = false) const PURE_VIRTUAL(UMovieGraphNode::GetNodeTitle, return FText(););

	/** Gets the category that the node belongs under. */
	virtual FText GetMenuCategory() const PURE_VIRTUAL(UMovieGraphNode::GetMenuCategory, return FText(); );
#endif

protected:
	virtual TArray<FMovieGraphPinProperties> GetExposedDynamicPinProperties() const;

	/** Register any delegates that need to be set up on the node. Called in PostLoad(). */
	virtual void RegisterDelegates() const { }

protected:
	UPROPERTY()
	TArray<TObjectPtr<UMovieGraphPin>> InputPins;
	
	UPROPERTY()
	TArray<TObjectPtr<UMovieGraphPin>> OutputPins;

	UPROPERTY(EditAnywhere, meta=(FixedLayout, ShowOnlyInnerProperties), Category = "Node")
	FInstancedPropertyBag DynamicProperties;

	UPROPERTY()
	TArray<FName> ExposedDynamicPropertyNames;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	int32 NodePosX = 0;

	UPROPERTY()
	int32 NodePosY = 0;

	UPROPERTY()
	FString NodeComment;

	UPROPERTY()
	uint8 bIsCommentBubblePinned : 1;

	UPROPERTY()
	uint8 bIsCommentBubbleVisible : 1;
#endif

	/** A GUID which uniquely identifies this node. */
	UPROPERTY()
	FGuid Guid;
};

/**
* Nodes representing user settings should derive from this. This is the only node type copied into flattened eval.
*/
UCLASS(Abstract, BlueprintType)
class MOVIERENDERPIPELINECORE_API UMovieGraphSettingNode : public UMovieGraphNode
{
	GENERATED_BODY()
public:
	// UMovieGraphNode Interface
	virtual TArray<FMovieGraphPinProperties> GetInputPinProperties() const override;
	virtual TArray<FMovieGraphPinProperties> GetOutputPinProperties() const override;
	// ~UMovieGraphNode Interface
};