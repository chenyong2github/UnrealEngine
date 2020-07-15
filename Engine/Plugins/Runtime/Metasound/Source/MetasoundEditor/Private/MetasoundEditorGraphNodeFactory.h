// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraphUtilities.h"
#include "GraphNode.h"
#include "SGraphNode.h"
#include "SGraphNodeSoundResult.h"
#include "SGraphNodeSoundBase.h"
#include "Widgets/DeclarativeSyntaxSupport.h"


class FMetasoundGraphNodeFactory : public FGraphPanelNodeFactory
{
	virtual TSharedPtr<SGraphNode> CreateNode(UEdGraphNode* InNode) const override
	{
		if (UMetasoundEditorGraphNode* BaseSoundNode = Cast<UMetasoundEditorGraphNode>(InNode))
		{
			return SNew(SGraphNodeSoundResult, RootSoundNode);
		}

		return nullptr;
	}
};
