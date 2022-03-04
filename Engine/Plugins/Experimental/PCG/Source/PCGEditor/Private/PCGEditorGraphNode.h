// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "EdGraph/EdGraphNode.h"

#include "PCGEditorGraphNode.generated.h"

class UPCGNode;
class UToolMenu;

UENUM()
enum class EPCGEditorGraphNodeType : uint8
{
	Input,
	Output,
	Settings
};

UCLASS()
class UPCGEditorGraphNode : public UEdGraphNode
{
	GENERATED_BODY()

public:
	void Construct(UPCGNode* InPCGNode, EPCGEditorGraphNodeType InNodeType);

	// ~Begin UEdGraphNode interface
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual void GetNodeContextMenuActions(UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const override;
	virtual void AllocateDefaultPins() override;
	// ~End UEdGraphNode interface

	UPCGNode* GetPCGNode() { return PCGNode; }

private:
	UPROPERTY()
	TObjectPtr<UPCGNode> PCGNode = nullptr;

 	UPROPERTY()
 	EPCGEditorGraphNodeType NodeType = EPCGEditorGraphNodeType::Settings;
};
