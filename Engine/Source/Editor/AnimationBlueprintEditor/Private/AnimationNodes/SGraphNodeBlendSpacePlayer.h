// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/Visibility.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "SNodePanel.h"
#include "AnimationNodes/SAnimationGraphNode.h"
#include "Animation/BlendSpaceBase.h"

class SVerticalBox;
class UAnimGraphNode_Base;

class SGraphNodeBlendSpacePlayer : public SAnimationGraphNode
{
public:
	SLATE_BEGIN_ARGS(SGraphNodeBlendSpacePlayer){}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UAnimGraphNode_Base* InNode);

	// SGraphNode interface
	virtual void CreateBelowWidgetControls(TSharedPtr<SVerticalBox> MainBox) override;
	// End of SGraphNode interface

protected:
	EVisibility GetBlendSpaceVisibility() const;
	bool GetBlendSpaceInfo(TWeakObjectPtr<const UBlendSpaceBase>& OutBlendSpace, FVector& OutPosition) const;

	TWeakObjectPtr<const UBlendSpaceBase> CachedBlendSpace;
	FVector CachedPosition;
};
