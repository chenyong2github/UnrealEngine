// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

class UEdGraphPin;
class UOptimusNode;
class UOptimusNodePin;
enum EEdGraphPinDirection;
struct FEdGraphPinType;

namespace OptimusEditor
{
	UOptimusNode* GetModelNodeFromGraphPin(const UEdGraphPin* InGraphPin);

	UOptimusNodePin* GetModelPinFromGraphPin(const UEdGraphPin* InGraphPin);

	FName GetAdderPinName(EEdGraphPinDirection InDirection);
		
	FText GetAdderPinFriendlyName(EEdGraphPinDirection InDirection);

	FName GetAdderPinCategoryName();

	bool IsAdderPin(const UEdGraphPin* InGraphPin);

	bool IsAdderPinType(const FEdGraphPinType& InPinType);
}
