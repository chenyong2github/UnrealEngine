// Copyright Epic Games, Inc. All Rights Reserved.

#include "Nodes/CustomizableObjectNodeMaterialBase.h"
#include "CustomizableObjectEditorModule.h"


bool UCustomizableObjectNodeMaterialBase::ShouldBreakExistingConnections(const UEdGraphPin* InputPin, const UEdGraphPin* OutputPin) const
{
	return false;
}
