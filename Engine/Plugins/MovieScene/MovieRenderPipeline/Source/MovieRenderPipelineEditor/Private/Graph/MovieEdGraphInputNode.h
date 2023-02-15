// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Graph/MovieEdGraphNode.h"

#include "MovieEdGraphInputNode.generated.h"


UCLASS()
class UMoviePipelineEdGraphNodeInput : public UMoviePipelineEdGraphNodeBase
{
	GENERATED_BODY()

public:
	// ~Begin UEdGraphNode interface
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual void AllocateDefaultPins() override;
	virtual bool CanUserDeleteNode() const override { return false; }
	virtual bool CanDuplicateNode() const override { return false; }
	// ~End UEdGraphNode interface
};
