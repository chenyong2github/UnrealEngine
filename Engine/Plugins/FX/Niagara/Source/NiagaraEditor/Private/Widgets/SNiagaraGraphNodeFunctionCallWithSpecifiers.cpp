// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SNiagaraGraphNodeFunctionCallWithSpecifiers.h"
#include "ISinglePropertyView.h"
#include "Modules/ModuleManager.h"
#include "NiagaraNodeFunctionCall.h"
#include "GraphEditorSettings.h"
#include "Widgets/Input/SEditableTextBox.h"

#define LOCTEXT_NAMESPACE "SNiagaraGraphNodeFunctionCallWithSpecifiers"

void SNiagaraGraphNodeFunctionCallWithSpecifiers::Construct(const FArguments& InArgs, UEdGraphNode* InGraphNode)
{
	GraphNode = InGraphNode;
	this->FunctionSpecifiers = &(dynamic_cast<UNiagaraNodeFunctionCall*>(GraphNode)->FunctionSpecifiers);
	RegisterNiagaraGraphNode(InGraphNode);
	this->UpdateGraphNode();
}

TSharedRef<SWidget> SNiagaraGraphNodeFunctionCallWithSpecifiers::CreateNodeContentArea()
{
	TSharedPtr<SVerticalBox> FunctionSpecifierWidget = SNew(SVerticalBox);
	for (TTuple<FName, FName>& Entry : *FunctionSpecifiers)
	{
		FunctionSpecifierWidget->AddSlot()
			.VAlign(VAlign_Center)
			[
				SNew(SNiagaraFunctionSpecifier, Entry.Key, Entry.Value, *FunctionSpecifiers)
			];
	}

	TSharedRef<SWidget> ContentAreaWidget = SGraphNode::CreateNodeContentArea();
	TSharedPtr<SVerticalBox> VertContainer = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.VAlign(VAlign_Center)
		.Padding(Settings->GetInputPinPadding())
		[
			FunctionSpecifierWidget.ToSharedRef()
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			ContentAreaWidget
		];
	TSharedRef<SWidget> RetWidget = VertContainer.ToSharedRef();
	return RetWidget;
}

void SNiagaraFunctionSpecifier::Construct(const FArguments& InArgs, FName InAttributeName, FName InValueName, TMap<FName, FName>& InSpecifiers)
{
	AttributeName = InAttributeName;
	ValueName = InValueName;
	Specifiers = &InSpecifiers;
	ChildSlot
		.VAlign(VAlign_Fill)
		.HAlign(HAlign_Fill)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Fill)
			.AutoWidth()
			.Padding(5)
			[
				SNew(STextBlock)
				.Text(FText::FromName(AttributeName))
			]
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Fill)
			.AutoWidth()
			.Padding(5)
			[
				SNew(SEditableTextBox)
				.Text(FText::FromName(ValueName))
				.OnTextCommitted(this, &SNiagaraFunctionSpecifier::OnValueNameCommitted)
			]
		];
}

void SNiagaraFunctionSpecifier::OnValueNameCommitted(const FText& InText, ETextCommit::Type InCommitType)
{
	ValueName = FName(*InText.ToString());
	if (Specifiers)
	{
		Specifiers->Add(AttributeName, ValueName);
	}
}

#undef LOCTEXT_NAMESPACE