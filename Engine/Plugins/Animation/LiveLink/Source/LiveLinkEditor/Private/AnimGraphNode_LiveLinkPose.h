// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimGraphNode_Base.h"
#include "AnimNode_LiveLinkPose.h"

#include "AnimGraphNode_LiveLinkPose.generated.h"



UCLASS()
class UAnimGraphNode_LiveLinkPose : public UAnimGraphNode_Base
{
	GENERATED_BODY()

public:

	//~ UEdGraphNode interface
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FText GetMenuCategory() const;
	//~ End of UEdGraphNode

	//~ Begin UK2Node interface
	virtual void ConvertDeprecatedNode(UEdGraph* Graph, bool bOnlySafeChanges) override;
	//~ End UK2Node interface

public:

	UPROPERTY(EditAnywhere, Category = Settings)
	FAnimNode_LiveLinkPose Node;

};
