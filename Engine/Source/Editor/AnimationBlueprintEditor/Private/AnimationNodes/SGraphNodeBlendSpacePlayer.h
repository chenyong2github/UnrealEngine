// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/Visibility.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "SNodePanel.h"
#include "AnimationNodes/SAnimationGraphNode.h"
#include "Animation/BlendSpace.h"

class SVerticalBox;
class UAnimGraphNode_Base;

class SGraphNodeBlendSpacePlayer : public SAnimationGraphNode
{
public:
	SLATE_BEGIN_ARGS(SGraphNodeBlendSpacePlayer){}
	SLATE_END_ARGS()

	// Reverse index of the debug grid widget
	static const int32 DebugGridSlotReverseIndex = 2;
	
	void Construct(const FArguments& InArgs, UAnimGraphNode_Base* InNode);

	// SGraphNode interface
	virtual void CreateBelowPinControls(TSharedPtr<SVerticalBox> MainBox) override;
	// End of SGraphNode interface

protected:
	// Invalidates the node's label if we are syncing based on graph context
	void UpdateGraphSyncLabel();

	// Cached name to display when sync groups are dynamic
	FName CachedSyncGroupName;
};
