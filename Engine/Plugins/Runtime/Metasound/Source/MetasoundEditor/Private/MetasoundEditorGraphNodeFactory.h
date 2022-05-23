// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "EdGraphUtilities.h"
#include "EdGraph/EdGraphNode.h"
#include "MetasoundEditorGraphNode.h"
#include "SGraphNode.h"
#include "SMetasoundGraphNode.h"
#include "SMetasoundGraphNodeComment.h"
#include "Templates/SharedPointer.h"
#include "Widgets/DeclarativeSyntaxSupport.h"


class FMetasoundGraphNodeFactory : public FGraphPanelNodeFactory
{
	virtual TSharedPtr<SGraphNode> CreateNode(UEdGraphNode* InNode) const override
	{
		using namespace Metasound::Editor;

		if (InNode->IsA<UMetasoundEditorGraphNode>())
		{
			return SNew(SMetaSoundGraphNode, InNode);
		}
		else if (UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(InNode))
		{
			return SNew(SMetasoundGraphNodeComment, CommentNode);
		}

		return nullptr;
	}
};
