// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Stack/SNiagaraStackErrorItem.h"
#include "NiagaraEditorWidgetsStyle.h"
#include "NiagaraEditorStyle.h"
#include "EditorStyleSet.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"
#include "ViewModels/Stack/NiagaraStackErrorItem.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SNiagaraStackErrorItem"

void SNiagaraStackErrorItem::Construct(const FArguments& InArgs, UNiagaraStackErrorItem* InErrorItem, UNiagaraStackViewModel* InStackViewModel)
{
	ErrorItem = InErrorItem;
	StackViewModel = InStackViewModel;

	const FSlateBrush* IconBrush;
	switch (ErrorItem->GetStackIssue().GetSeverity())
	{
	case EStackIssueSeverity::Error:
		IconBrush = FEditorStyle::GetBrush("Icons.Error");
		break;
	case EStackIssueSeverity::Warning:
		IconBrush = FEditorStyle::GetBrush("Icons.Warning");
		break;
	case EStackIssueSeverity::Info:
		IconBrush = FEditorStyle::GetBrush("Icons.Info");
		break;
	default:
		IconBrush = FEditorStyle::GetBrush("NoBrush");
		break;
	}

	ChildSlot
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Left)
		.AutoWidth()
		[
			SNew(SImage)
			.Image(IconBrush)
		]
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Left)
		.AutoWidth()
		[
			SNew(STextBlock)
			.TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.ParameterText")
			.Text_UObject(ErrorItem, &UNiagaraStackErrorItem::GetDisplayName)
			.ColorAndOpacity(this, &SNiagaraStackErrorItem::GetTextColorForSearch)
			.HighlightText_UObject(StackViewModel, &UNiagaraStackViewModel::GetCurrentSearchText)
		]
	];
}

void SNiagaraStackErrorItemFix::Construct(const FArguments& InArgs, UNiagaraStackErrorItemFix* InErrorItem, UNiagaraStackViewModel* InStackViewModel)
{
	ErrorItem = InErrorItem;
	StackViewModel = InStackViewModel;

	ChildSlot
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.Padding(0, 4, 0, 0)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.ParameterText")
			.Text_UObject(ErrorItem, &UNiagaraStackErrorItemFix::GetDisplayName)
			.ColorAndOpacity(this, &SNiagaraStackErrorItemFix::GetTextColorForSearch)
			.HighlightText_UObject(StackViewModel, &UNiagaraStackViewModel::GetCurrentSearchText)
			.AutoWrapText(true)
		]
		+ SHorizontalBox::Slot()
		.Padding(5, 0, 0, 0)
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.ParameterText")
			.Text_UObject(ErrorItem, &UNiagaraStackErrorItemFix::GetFixButtonText)
			.OnClicked_UObject(ErrorItem, &UNiagaraStackErrorItemFix::OnTryFixError)
		]
	];
}

#undef LOCTEXT_NAMESPACE //"SNiagaraStackErrorItem"

