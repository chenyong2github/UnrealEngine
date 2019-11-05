// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimGraphNode_Base.h"
#include "AnimNode_SteamVRInputAnimPose.h"

#include "AnimGraphNode_SteamVRInputAnimPose.generated.h"

using namespace vr;

UCLASS()
class UAnimGraphNode_SteamVRInputAnimPose : public UAnimGraphNode_Base
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(EditAnywhere, Category=Settings)
	FAnimNode_SteamVRInputAnimPose Node;

public:
	// UEdGraphNode interface
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FText GetMenuCategory() const override;
	virtual FText GetTooltipText() const override;
	// End of UEdGraphNode interface
};