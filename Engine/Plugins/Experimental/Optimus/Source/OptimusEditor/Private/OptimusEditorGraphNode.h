// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EdGraph/EdGraphNode.h"

#include "Containers/Map.h"

#include "OptimusEditorGraphNode.generated.h"

class UOptimusNode;
class UOptimusNodePin;

UCLASS()
class UOptimusEditorGraphNode : public UEdGraphNode
{
	GENERATED_BODY()

public:
	void Construct(UOptimusNode* InNode);

	UOptimusNodePin* FindModelPinFromGraphPin(const UEdGraphPin* InGraphPin);
	UEdGraphPin* FindGraphPinFromModelPin(const UOptimusNodePin* InModelPin);

	/// Synchronize the stored value on the graph pin with the value stored on the node. 
	/// 
	void SynchronizeGraphPinValueWithModelPin(UEdGraphPin* InGraphPin);

	// UEdGraphNode overrides
	FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;

	UPROPERTY()
	UOptimusNode *ModelNode = nullptr;

private:
	void CreateGraphPinFromModelPin(
		const UOptimusNodePin* InModelPin,
		EEdGraphPinDirection InDirection, 
		UEdGraphPin *InParentPin = nullptr
	);

	TMap<FName, UOptimusNodePin*> PathToModelPinMap;
	TMap<FName, UEdGraphPin*> PathToGraphPinMap;
};
