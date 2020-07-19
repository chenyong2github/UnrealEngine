// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EdGraph/EdGraphSchema.h"

#include "OptimusEditorGraphSchema.generated.h"

namespace OptimusSchemaPinTypes
{
	extern FName Attribute;
	extern FName Skeleton;
	extern FName Mesh;
}


UCLASS()
class UOptimusEditorGraphSchema : public UEdGraphSchema
{
	GENERATED_BODY()
public:

	static const FName GraphName_OptimusDeformer;

	UOptimusEditorGraphSchema();

	void GetGraphActions(
		FGraphActionListBuilderBase& IoActionBuilder,
		const UEdGraphPin *InFromPin,
		const UEdGraph* InGraph) const;

	// UEdGraphSchema overrides
	bool TryCreateConnection(UEdGraphPin* PinA, UEdGraphPin* PinB) const;
	const FPinConnectionResponse CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const override;
	void BreakPinLinks(UEdGraphPin& TargetPin, bool bSendsNodeNotifcation) const override;
	void BreakSinglePinLink(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin) const override;
	void GetGraphContextActions(FGraphContextMenuBuilder& IoContextMenuBuilder) const override;
	bool SafeDeleteNodeFromGraph(UEdGraph* Graph, UEdGraphNode* Node) const override;

	FLinearColor GetPinTypeColor(const FEdGraphPinType& PinType) const override;

};


/// Action to add a new Optimus node to the graph
USTRUCT()
struct FOptimusGraphSchemaAction_NewNode
	: public FEdGraphSchemaAction
{
	GENERATED_BODY()

	// Inherit the base class's constructors
	using FEdGraphSchemaAction::FEdGraphSchemaAction;

	UPROPERTY()
	UClass* NodeClass = nullptr;

	static FName StaticGetTypeId() { static FName Type("FOptimusDeformerGraphSchemaAction_NewNode"); return Type; }
	virtual FName GetTypeId() const override { return StaticGetTypeId(); }

	// FEdGraphSchemaAction overrides
	UEdGraphNode* PerformAction(class UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode = true) override;
};
