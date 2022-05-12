// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"

#include "PCGCommon.h"

#include "PCGEditorGraphNodeBase.generated.h"

class UPCGNode;
class UPCGPin;
class UToolMenu;

UENUM()
enum class EPCGEditorGraphNodeType : uint8
{
	Input,
	Output,
	Settings
};

UCLASS()
class UPCGEditorGraphNodeBase : public UEdGraphNode
{
	GENERATED_BODY()

public:
	void Construct(UPCGNode* InPCGNode, EPCGEditorGraphNodeType InNodeType);

	// ~Begin UObject interface
	void BeginDestroy() override;
	// ~End UObject interface

	// ~Begin UEdGraphNode interface
	virtual void GetNodeContextMenuActions(UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const override;
	virtual void AutowireNewNode(UEdGraphPin* FromPin) override;
	virtual void PrepareForCopying() override;
	virtual bool CanCreateUnderSpecifiedSchema(const UEdGraphSchema* Schema) const override;
	virtual void ReconstructNode() override;
	virtual FLinearColor GetNodeTitleColor() const;
	virtual void PostPasteNode() override;
	// ~End UEdGraphNode interface

	UPCGNode* GetPCGNode() { return PCGNode; }
	const UPCGNode* GetPCGNode() const { return PCGNode; }
	void PostCopy();
	void PostPaste();

	DECLARE_DELEGATE(FOnPCGEditorGraphNodeChanged);
	FOnPCGEditorGraphNodeChanged OnNodeChangedDelegate;

protected:
	static FEdGraphPinType GetPinType(const UPCGPin* InPin);

	void RebuildEdgesFromPins();

	void OnNodeChanged(UPCGNode* InNode, EPCGChangeType ChangeType);
	void OnPickColor();
	void OnColorPicked(FLinearColor NewColor);

	UPROPERTY()
	TObjectPtr<UPCGNode> PCGNode = nullptr;

 	UPROPERTY()
 	EPCGEditorGraphNodeType NodeType = EPCGEditorGraphNodeType::Settings;

	bool bDisableReconstructFromNode = false;
};
