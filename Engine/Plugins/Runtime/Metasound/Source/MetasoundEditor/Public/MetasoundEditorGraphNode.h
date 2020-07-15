// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "MetasoundEditorGraphNode_Base.h"

#include "MetaSoundEditorGraphNode.generated.h"

// Forward Declarations
class UEdGraphPin;
class USoundNode;


UCLASS(MinimalAPI)
class UMetasoundEditorGraphNode : public UMetasoundEditorGraphNode_Base
{
	GENERATED_UCLASS_BODY()

	/** Fix up the node's owner after being copied */
	METASOUNDEDITOR_API void PostCopyNode();
	/** Create a new input pin for this node */
	METASOUNDEDITOR_API void CreateInputPin();
	/** Add an input pin to this node and recompile the MetaSoundEditor */
	METASOUNDEDITOR_API void AddInputPin();
	/** Remove a specific input pin from this node and recompile the MetaSoundEditor */
	METASOUNDEDITOR_API void RemoveInputPin(UEdGraphPin* InGraphPin);
	/** Estimate the width of this Node from the length of its title */
	METASOUNDEDITOR_API int32 EstimateNodeWidth() const;
	/** Checks whether an input can be added to this node */
	METASOUNDEDITOR_API bool CanAddInputPin() const;


	// UMetasoundEditorGraphNode_Base interface
	virtual void CreateInputPins() override;
	// End of UMetasoundEditorGraphNode_Base interface

	// UEdGraphNode interface
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual void PrepareForCopying() override;
	virtual void GetNodeContextMenuActions(class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const override;
	virtual FText GetTooltipText() const override;
	virtual FString GetDocumentationExcerptName() const override;
	// End of UEdGraphNode interface

	// UObject interface
	virtual void PostLoad() override;
	virtual void PostEditImport() override;
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
	// End of UObject interface
};
