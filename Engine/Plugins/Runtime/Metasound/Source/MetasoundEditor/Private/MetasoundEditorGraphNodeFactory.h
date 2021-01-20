// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "EdGraphUtilities.h"
#include "MetasoundEditorGraphNode.h"
#include "SGraphNode.h"
#include "SMetasoundGraphNode.h"
#include "Widgets/DeclarativeSyntaxSupport.h"


class FMetasoundGraphNodeFactory : public FGraphPanelNodeFactory
{
	virtual TSharedPtr<SGraphNode> CreateNode(UEdGraphNode* InNode) const override
	{
		if (InNode->IsA<UMetasoundEditorGraphNode>())
		{
			return SNew(SMetasoundGraphNode, InNode);
		}

		return nullptr;
	}
};
