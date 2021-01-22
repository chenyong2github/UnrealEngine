// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "KismetCompilerMisc.h"

class UK2Node;
class UFunction;
class UEdGraphPin;

class BLUEPRINTGRAPH_API FBlueprintNodeStatics
{
public:

	static UEdGraphPin* CreateSelfPin(UK2Node* Node, const UFunction* Function);

	static bool CreateParameterPinsForFunction(UK2Node* Node, const UFunction* Function, TFunctionRef<void(UEdGraphPin* /*Pin*/)> PostParameterPinCreatedCallback);
};