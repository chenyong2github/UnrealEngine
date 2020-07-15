// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"
#include "UObject/ObjectMacros.h"

#include "MetasoundEditorGraphNode_Base.generated.h"

class UEdGraphPin;
class UEdGraphSchema;

UCLASS(MinimalAPI)
class UMetasoundEditorGraphNode_Base : public UEdGraphNode
{
	GENERATED_UCLASS_BODY()

	/** Create input pins required of the node */
	virtual void CreateInputPins() { };

	/** Whether or not this is the root node */
	virtual bool IsRootNode() const { return false; }

	/** Get the Output Pin (should only ever be one) */
	METASOUNDEDITOR_API class UEdGraphPin* GetOutputPin();

	/** Get all of the input pins */
	METASOUNDEDITOR_API void GetInputPins(TArray<UEdGraphPin*>& OutInputPins);

	/** Get the input pin at the provided index */
	METASOUNDEDITOR_API class UEdGraphPin* GetInputPin(int32 InputIndex);

	/** Get the current input pin count */
	METASOUNDEDITOR_API int32 GetInputCount() const;

	/**
	 * Handles inserting the node between the FromPin and what the FromPin was original connected to
	 *
	 * @param FromPin		The pin this node is being spawned from
	 * @param NewLinkPin	The new pin the FromPin will connect to
	 * @param OutNodeList	Any nodes that are modified will get added to this list for notification purposes
	 */
	void InsertNewNode(UEdGraphPin* FromPin, UEdGraphPin* NewLinkPin, TSet<UEdGraphNode*>& OutNodeList);

	// UEdGraphNode interface.
	virtual void AllocateDefaultPins() override;
	virtual void ReconstructNode() override;
	virtual void AutowireNewNode(UEdGraphPin* FromPin) override;
	virtual bool CanCreateUnderSpecifiedSchema(const UEdGraphSchema* Schema) const override;
	virtual FString GetDocumentationLink() const override;
	// End of UEdGraphNode interface.
};
