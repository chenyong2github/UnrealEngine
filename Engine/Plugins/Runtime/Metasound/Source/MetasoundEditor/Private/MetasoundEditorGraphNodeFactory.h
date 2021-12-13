// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "EdGraphUtilities.h"
#include "EdGraph/EdGraphNode.h"
#include "MetasoundEditorGraphNode.h"
#include "SGraphNode.h"
#include "SMetasoundGraphNode.h"
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

		return nullptr;
	}
};
