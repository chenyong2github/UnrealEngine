// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusEditorGraphPinFactory.h"
#include "OptimusEditorGraph.h"

#include "NodeFactory.h"


TSharedPtr<class SGraphPin> FOptimusEditorGraphPinFactory::CreatePin(class UEdGraphPin* InPin) const
{
	if (InPin)
	{
		if (const UEdGraphNode* OwningNode = InPin->GetOwningNode())
		{
			// only create pins within optimus graphs
			if (Cast<UOptimusEditorGraph>(OwningNode->GetGraph()) == nullptr)
			{
				return nullptr;
			}
		}

		// FIXME: Add specializations here.
	}

	TSharedPtr<SGraphPin> K2PinWidget = FNodeFactory::CreateK2PinWidget(InPin);
	if (K2PinWidget.IsValid())
	{
		return K2PinWidget;
	}

	return nullptr;
}
