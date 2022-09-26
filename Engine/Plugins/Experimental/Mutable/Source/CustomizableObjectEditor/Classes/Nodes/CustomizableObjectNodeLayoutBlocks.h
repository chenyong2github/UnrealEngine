// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Nodes/CustomizableObjectNode.h"
#include "CustomizableObjectLayout.h"

#include "CustomizableObjectNodeLayoutBlocks.generated.h"


UCLASS()
class CUSTOMIZABLEOBJECTEDITOR_API UCustomizableObjectNodeLayoutBlocks : public UCustomizableObjectNode
{
public:
	GENERATED_BODY()

	UCustomizableObjectNodeLayoutBlocks();

	UPROPERTY()
	FIntPoint GridSize_DEPRECATED;

	/** Used with the fixed layout strategy. */
	UPROPERTY()
	FIntPoint MaxGridSize_DEPRECATED;

	UPROPERTY()
	TArray<FCustomizableObjectLayoutBlock> Blocks_DEPRECATED;

	UPROPERTY()
	ECustomizableObjectTextureLayoutPackingStrategy PackingStrategy_DEPRECATED;

	UPROPERTY()
	TObjectPtr<UCustomizableObjectLayout> Layout = nullptr;

	// EdGraphNode interface
	FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	FLinearColor GetNodeTitleColor() const override;
	FText GetTooltipText() const override;
	void Serialize(FArchive& Ar) override;
	void PinConnectionListChanged(UEdGraphPin* Pin) override;
	void PostPasteNode() override;


	// UCustomizableObjectNode interface
	virtual void PostBackwardsCompatibleFixup() override;
	void AllocateDefaultPins(UCustomizableObjectNodeRemapPins* RemapPins) override;

	UEdGraphPin* OutputPin() const
	{
		return FindPin(TEXT("Layout"));
	}

	void AddAttachedErrorData(const FAttachedErrorDataView& AttachedErrorData) override;
	void ResetAttachedErrorData() override;

	bool IsSingleOutputNode() const override;

private:

	/** Last static or skeletal mesh connected. Used to remove the callback once disconnected. */
	TWeakObjectPtr<UCustomizableObjectNode> LastMeshNodeConnected;

	/** Connected NodeStaticMesh or NodeSkeletalMesh Mesh UPROPERTY changed callback function. Sets the layout mesh. */
	void MeshPostEditChangeProperty(UObject* Node, FPropertyChangedEvent& FPropertyChangedEvent);

	void LinkPostEditChangePropertyDelegate(UEdGraphPin* Pin);

	void SetLayoutSkeletalMesh();
};

