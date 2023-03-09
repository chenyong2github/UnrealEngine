// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreTypes.h"
#include "EdGraph/EdGraphNode.h"
#include "Graph/MovieGraphNode.h"

#include "MovieEdGraphNode.generated.h"

class UMovieGraphNode;

UCLASS(Abstract)
class UMoviePipelineEdGraphNodeBase : public UEdGraphNode
{
	GENERATED_BODY()
public:
	UMovieGraphNode* GetRuntimeNode() const { return RuntimeNode; }

	void Construct(UMovieGraphNode* InRuntimeNode);

	void OnRuntimeNodeChanged(const UMovieGraphNode* InChangedNode);

	//~ Begin UEdGraphNode Interface
	virtual void ReconstructNode() override;
	virtual void AutowireNewNode(UEdGraphPin* FromPin) override;
	//~ End UEdGraphNode Interface
	
	//~ Begin UObject interface
	virtual void PostTransacted(const FTransactionObjectEvent& TransactionEvent) override;
	//~ End UObject interface

protected:
	virtual bool ShouldCreatePin(const UMovieGraphPin* InPin) const;

	void CreatePins(const TArray<UMovieGraphPin*>& InInputPins, const TArray<UMovieGraphPin*>& InOutputPins);

	static FEdGraphPinType GetPinType(const UMovieGraphPin* InPin);

	/** Recreate the pins on this node, discarding all existing pins. */
	void ReconstructPins();

	void UpdatePosition();

protected:
	/** The runtime node that this editor node represents. */
	UPROPERTY()
	TObjectPtr<UMovieGraphNode> RuntimeNode;
};

UCLASS()
class UMoviePipelineEdGraphNode : public UMoviePipelineEdGraphNodeBase
{
	GENERATED_BODY()

public:
	//~ Begin UEdGraphNode Interface
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual void GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;
	//~ End UEdGraphNode Interface

protected:
	/** Promote the property with the given name to a pin exposed on the node. */
	void PromotePropertyToPin(const FName PropertyName) const;

private:
	void GetPropertyPromotionContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const;
};
