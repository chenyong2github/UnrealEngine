// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeMaterialBase.h"
#include "MuCOE/CustomizableObjectEditorModule.h"


bool UCustomizableObjectNodeMaterialBase::ShouldBreakExistingConnections(const UEdGraphPin* InputPin, const UEdGraphPin* OutputPin) const
{
	return false;
}
