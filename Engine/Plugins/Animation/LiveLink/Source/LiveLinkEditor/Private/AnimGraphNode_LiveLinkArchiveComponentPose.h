// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimGraphNode_Base.h"
#include "AnimNode_LiveLinkArchiveComponentPose.h"

#include "AnimGraphNode_LiveLinkArchiveComponentPose.generated.h"
UCLASS()
class UAnimGraphNode_LiveLinkArchiveComponentPose : public UAnimGraphNode_Base
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(EditAnywhere, Category = Settings)
	FAnimNode_LiveLinkArchiveComponentPose Node;

	// UEdGraphNode interface
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FText GetMenuCategory() const;
	// End of UEdGraphNode
};