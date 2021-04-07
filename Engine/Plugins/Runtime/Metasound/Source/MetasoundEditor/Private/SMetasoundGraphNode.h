// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

#include "Input/Reply.h"
#include "Layout/Visibility.h"
#include "SGraphNode.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SOverlay.h"


// Forward Declarations
class SGraphPin;
class SVerticalBox;
class UMetasoundEditorGraphInputLiteral;
class UMetasoundEditorGraphNode;


class SMetasoundGraphNode : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SMetasoundGraphNode)
	{
	}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, class UEdGraphNode* InNode);

protected:
	// SGraphNode Interface
	virtual void CreateOutputSideAddButton(TSharedPtr<SVerticalBox> OutputBox) override;
	virtual TSharedPtr<SGraphPin> CreatePinWidget(UEdGraphPin* InPin) const override;
	virtual void CreateStandardPinWidget(UEdGraphPin* InPin) override;
	virtual TSharedRef<SWidget> CreateNodeContentArea() override;
	virtual TSharedRef<SWidget> CreateTitleWidget(TSharedPtr<SNodeTitle> NodeTitle) override;
	virtual const FSlateBrush* GetNodeBodyBrush() const override;
	virtual TSharedRef<SWidget> CreateTitleRightWidget() override;
	virtual EVisibility IsAddPinButtonVisible() const override;
	virtual FReply OnAddPin() override;
	virtual void MoveTo(const FVector2D& NewPosition, FNodeSet& NodeFilter) override;
	virtual void SetDefaultTitleAreaWidget(TSharedRef<SOverlay> DefaultTitleAreaWidget) override;

	FName GetLiteralDataType() const;
	UMetasoundEditorGraphNode& GetMetasoundNode();
	const UMetasoundEditorGraphNode& GetMetasoundNode() const;

public:
	static void ExecuteInputTrigger(UMetasoundEditorGraphInputLiteral& Literal);
	static TSharedRef<SWidget> CreateTriggerSimulationWidget(UMetasoundEditorGraphInputLiteral& Literal);
};
