// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusEditorGraphPinFactory.h"

#include "NodeFactory.h"


TSharedPtr<class SGraphPin> FOptimusEditorGraphPinFactory::CreatePin(class UEdGraphPin* InPin) const
{
	if (InPin)
	{
		// FIXME: Add specializations here.
	}

	TSharedPtr<SGraphPin> K2PinWidget = FNodeFactory::CreateK2PinWidget(InPin);
	if (K2PinWidget.IsValid())
	{
		return K2PinWidget;
	}

	return nullptr;
}
