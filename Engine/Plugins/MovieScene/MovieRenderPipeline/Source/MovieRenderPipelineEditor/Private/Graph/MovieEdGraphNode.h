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
	void SetRuntimeNode(UMovieGraphNode* InNode)
	{
		RuntimeNode = InNode;
		InNode->GraphNode = this;
	}

	UMovieGraphNode* GetRuntimeNode() const { return RuntimeNode; }

	void Construct(UMovieGraphNode* InRuntimeNode);
protected:
	virtual bool ShouldCreatePin(const UMovieGraphPin* InPin) const;

	void CreatePins(const TArray<UMovieGraphPin*>& InInputPins, const TArray<UMovieGraphPin*>& InOutputPins);

	static FEdGraphPinType GetPinType(const UMovieGraphPin* InPin);
	
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
	//~ End UEdGraphNode Interface


};
