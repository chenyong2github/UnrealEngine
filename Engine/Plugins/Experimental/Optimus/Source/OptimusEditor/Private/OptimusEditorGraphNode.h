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
	/// If the pin has sub-pins, the value update is done recursively.
	void SynchronizeGraphPinValueWithModelPin(const UOptimusNodePin* InModelPin);

	// UEdGraphNode overrides
	FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;

	
	// FIXME: Move to private and add accessor function.
	UPROPERTY()
	UOptimusNode *ModelNode = nullptr;

protected:
	friend class SOptimusEditorGraphNode;

	const TArray<UOptimusNodePin*>* GetTopLevelInputPins() const { return &TopLevelInputPins; }
	const TArray<UOptimusNodePin*>* GetTopLevelOutputPins() const { return &TopLevelOutputPins; }

private:
	void CreateGraphPinFromModelPin(
		const UOptimusNodePin* InModelPin,
		EEdGraphPinDirection InDirection, 
		UEdGraphPin *InParentPin = nullptr
	);

	void UpdateTopLevelPins();

	TMap<FName, UOptimusNodePin*> PathToModelPinMap;
	TMap<FName, UEdGraphPin*> PathToGraphPinMap;

	// These need to be always-living arrays because of the way STreeView works. See
	// SOptimusEditorGraphNode for usage.
	TArray<UOptimusNodePin*> TopLevelInputPins;
	TArray<UOptimusNodePin*> TopLevelOutputPins;
};
