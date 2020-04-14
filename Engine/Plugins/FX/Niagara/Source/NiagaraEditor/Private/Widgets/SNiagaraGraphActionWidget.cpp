// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNiagaraGraphActionWidget.h"

#include "NiagaraActions.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SNiagaraParameterName.h"
#include "NiagaraEditorStyle.h"

#define LOCTEXT_NAMESPACE "NiagaraGraphActionWidget"

void SNiagaraGraphActionWidget::Construct(const FArguments& InArgs, const FCreateWidgetForActionData* InCreateData)
{
	ActionPtr = InCreateData->Action;
	MouseButtonDownDelegate = InCreateData->MouseButtonDownDelegate;

	TSharedPtr<FNiagaraMenuAction> NiagaraAction = StaticCastSharedPtr<FNiagaraMenuAction>(InCreateData->Action);

	TSharedPtr<SWidget> NameWidget;
	if (NiagaraAction->GetParameterVariable().IsSet())
	{
		NameWidget = SNew(SNiagaraParameterName)
			.ParameterName(NiagaraAction->GetParameterVariable()->GetName())
			.IsReadOnly(true)
			.HighlightText(InArgs._HighlightText)
			.DecoratorHAlign(HAlign_Right)
			.Decorator()
			[
				SNew(SBox)
				.Padding(FMargin(7.0f, 0.0f, 0.0f, 0.0f))
				[
					SNew(STextBlock)
					.TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.ParameterName.TypeText")
					.Text(NiagaraAction->GetParameterVariable()->GetType().GetNameText())
				]
			];
	}
	else
	{
		NameWidget = SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.Text(InCreateData->Action->GetMenuDescription())
			.HighlightText(InArgs._HighlightText);
	}

	this->ChildSlot
	[
		SNew(SHorizontalBox)
		.ToolTipText(InCreateData->Action->GetTooltipDescription())
		+ SHorizontalBox::Slot()
		.FillWidth(1)
		.VAlign(VAlign_Center)
		[
			NameWidget.ToSharedRef()
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Right)
		[
			SNew(SImage)
			.Image(NiagaraAction->IsExperimental ? FEditorStyle::GetBrush("Icons.Info") : nullptr)
			.Visibility(NiagaraAction->IsExperimental ? EVisibility::Visible : EVisibility::Collapsed)
			.ToolTipText(NiagaraAction->IsExperimental ? LOCTEXT("ScriptExperimentalToolTip", "This script is experimental, use with care!") : FText::GetEmpty())
		]
	];
}

FReply SNiagaraGraphActionWidget::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseButtonDownDelegate.Execute(ActionPtr))
	{
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
