// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "KismetNodes/SGraphNodeK2Composite.h"

class UEdGraph;
class UAnimGraphNode_BlendSpaceGraphBase;

class SGraphNodeBlendSpaceGraph : public SGraphNodeK2Composite
{
public:
	SLATE_BEGIN_ARGS(SGraphNodeBlendSpaceGraph){}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, class UAnimGraphNode_BlendSpaceGraphBase* InNode);

	// SGraphNode interface
	virtual void UpdateGraphNode() override;
	// End of SGraphNode interface

protected:
	// SGraphNodeK2Composite interface
	virtual UEdGraph* GetInnerGraph() const override;

	// SGraphNode interface
	TSharedPtr<SToolTip> GetComplexTooltip() override;
};
