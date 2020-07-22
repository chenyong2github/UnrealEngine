// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SGraphNode.h"
#include "Widgets/DeclarativeSyntaxSupport.h"


class UOptimusEditorGraphNode;


class SOptimusEditorGraphNode : 
	public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SOptimusEditorGraphNode)
		: _GraphNode(nullptr)
	{}
		SLATE_ARGUMENT(UOptimusEditorGraphNode*, GraphNode)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// SGraphNode overrides
	void EndUserInteraction() const override;

private:

};
