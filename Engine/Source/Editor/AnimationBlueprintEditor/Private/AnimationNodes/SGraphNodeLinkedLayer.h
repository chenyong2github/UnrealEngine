// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/Visibility.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "SNodePanel.h"
#include "AnimationNodes/SAnimationGraphNode.h"
#include "Animation/BlendSpace.h"

class UAnimGraphNode_Base;

class SGraphNodeLinkedLayer : public SAnimationGraphNode
{
public:
	SLATE_BEGIN_ARGS(SGraphNodeLinkedLayer){}
	SLATE_END_ARGS()
	
	void Construct(const FArguments& InArgs, UAnimGraphNode_Base* InNode);

protected:
	void UpdateNodeLabel();

	FName CachedTargetName;
};
