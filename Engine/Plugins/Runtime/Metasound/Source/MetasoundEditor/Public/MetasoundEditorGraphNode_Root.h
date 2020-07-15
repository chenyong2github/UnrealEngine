// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundEditorGraphNode_Base.h"
#include "UObject/ObjectMacros.h"

#include "MetasoundEditorGraphNode_Root.generated.h"

// Forward Declarations
class UGraphNodeContextMenuContext;

UCLASS(MinimalAPI)
class UMetasoundEditorGraphNode_Root : public UMetasoundEditorGraphNode_Base
{
	GENERATED_UCLASS_BODY()

	// UEdGraphNode interface
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual FText GetTooltipText() const override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual bool CanUserDeleteNode() const override { return false; }
	virtual bool CanDuplicateNode() const override { return false; }
	virtual void GetNodeContextMenuActions(class UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;
	// End of UEdGraphNode interface

	// UMetasoundEditorGraphNode_Base interface
	virtual void CreateInputPins() override;
	virtual bool IsRootNode() const override { return true; }
	// End of UMetasoundEditorGraphNode_Base interface
};
