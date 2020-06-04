// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Editor/AnimGraph/Classes/AnimGraphNode_SkeletalControlBase.h"
#include "BoneControllers/AnimNode_OrientationWarping.h"
#include "AnimGraphNode_OrientationWarping.generated.h"

UCLASS()
class PUMAEDITOR_API UAnimGraphNode_OrientationWarping : public UAnimGraphNode_SkeletalControlBase
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(EditAnywhere, Category = Settings)
	FAnimNode_OrientationWarping Node;

public:
	// UEdGraphNode interface
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	// End of UEdGraphNode interface

protected:
	// UAnimGraphNode_SkeletalControlBase interface
	virtual FText GetControllerDescription() const override;
	virtual const FAnimNode_SkeletalControlBase* GetNode() const override { return &Node; }
	// End of UAnimGraphNode_SkeletalControlBase interface
};
