// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNiagaraParameterMapPaletteItem.h"
#include "NiagaraActions.h"
#include "EdGraphSchema_Niagara.h"
#include "TutorialMetaData.h"
#include "NiagaraGraph.h"
#include "NiagaraEditorStyle.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Images/SImage.h"
#include "ScopedTransaction.h"
#include "Widgets/SNiagaraParameterName.h"

#define LOCTEXT_NAMESPACE "NiagaraParameterMapPalleteItem"

void SNiagaraParameterMapPalleteItem::Construct(const FArguments& InArgs, FCreateWidgetForActionData* const InCreateData )
{
	this->OnItemRenamed = InArgs._OnItemRenamed;

	TAttribute<bool> bIsReadOnly = false;
	TAttribute<bool> bIsEditingEnabled = true;

	check(InCreateData->Action.IsValid());
	TSharedPtr<FNiagaraParameterAction> ParameterAction = StaticCastSharedPtr<FNiagaraParameterAction>(InCreateData->Action);
	ActionPtr = InCreateData->Action;

	FTutorialMetaData TagMeta("PaletteItem");

	const FLinearColor TypeColor = UEdGraphSchema_Niagara::GetTypeColor(ParameterAction->GetParameter().GetType());
	FSlateBrush const* IconBrush = FEditorStyle::GetBrush(TEXT("Kismet.AllClasses.VariableIcon"));
	FSlateBrush const* SecondaryBrush = FEditorStyle::GetBrush(TEXT("NoBrush"));
	FSlateColor        IconColor = FSlateColor(TypeColor);
	FSlateColor        SecondaryIconColor = IconColor;
	FText			   IconToolTip = FText::GetEmpty();
	FString			   IconDocLink, IconDocExcerpt;
	TSharedRef<SWidget> IconWidget = CreateIconWidget(IconToolTip, IconBrush, IconColor, IconDocLink, IconDocExcerpt, SecondaryBrush, SecondaryIconColor);
	IconWidget->SetEnabled(bIsEditingEnabled);

	static const FName BoldFontName = FName("Bold");
	static const FName ItalicFontName = FName("Italic");
	const FName FontType = ItalicFontName;
	FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(FontType, 10);

	if (InCreateData->bHandleMouseButtonDown)
	{
		MouseButtonDownDelegate = InCreateData->MouseButtonDownDelegate;
	}

	ParameterNameTextBlock = SNew(SNiagaraParameterNameTextBlock)
		.ParameterText(this, &SNiagaraParameterMapPalleteItem::GetDisplayText)
		//.HighlightText(InCreateData->HighlightText)
		.ToolTipText(this, &SNiagaraParameterMapPalleteItem::GetItemTooltip)
		.OnTextCommitted(this, &SNiagaraParameterMapPalleteItem::OnNameTextCommitted)
		.OnVerifyTextChanged(this, &SNiagaraParameterMapPalleteItem::OnNameTextVerifyChanged)
		.IsSelected(InCreateData->IsRowSelectedDelegate)
		.IsReadOnly(InCreateData->bIsReadOnly);

	InCreateData->OnRenameRequest->BindSP(ParameterNameTextBlock.ToSharedRef(), &SNiagaraParameterNameTextBlock::EnterEditingMode);

	// now, create the actual widget
	ChildSlot
	[
		SNew(SHorizontalBox)
		.AddMetaData<FTutorialMetaData>(TagMeta)
		.ToolTipText(ParameterAction->GetTooltipDescription())
		// icon slot
		+SHorizontalBox::Slot()
		.AutoWidth()
		[
			IconWidget
		]
		// name slot
		+SHorizontalBox::Slot()
		.FillWidth(1.f)
		.VAlign(VAlign_Center)
		.Padding(5,0)
		[
			ParameterNameTextBlock.ToSharedRef()
		]
		// reference count
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(3, 0)
		[
			SNew(SComboButton)
			.HasDownArrow(false)
			.ButtonStyle(FEditorStyle::Get(), "RoundButton")
			.ForegroundColor(FSlateColor::UseForeground())
			.ContentPadding(FMargin(2.0f))
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text(this, &SNiagaraParameterMapPalleteItem::GetReferenceCount)
				.Font(Font)
			]
		]

	];
}

void SNiagaraParameterMapPalleteItem::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SGraphPaletteItem::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (ParameterNameTextBlock.IsValid())
	{
		TSharedPtr<FNiagaraParameterAction> ParameterAction = StaticCastSharedPtr<FNiagaraParameterAction>(ActionPtr.Pin());
		if (ParameterAction.IsValid() && ParameterAction->bSubnamespaceRenamePending)
		{
			ParameterAction->bSubnamespaceRenamePending = false;
			ParameterNameTextBlock->EnterSubnamespaceEditingMode();
		}
	}
}

void SNiagaraParameterMapPalleteItem::OnNameTextCommitted(const FText& NewText, ETextCommit::Type InTextCommit)
{
	TSharedPtr<FNiagaraParameterAction> ParameterAction = StaticCastSharedPtr<FNiagaraParameterAction>(ActionPtr.Pin());
	FScopedTransaction RenameParametersWithPins(LOCTEXT("RenameParameter", "Rename parameter, referenced pins and metadata"));
	OnItemRenamed.ExecuteIfBound(NewText, *ParameterAction.Get());
}

FText SNiagaraParameterMapPalleteItem::GetReferenceCount() const
{
	TSharedPtr<FNiagaraParameterAction> ParameterAction = StaticCastSharedPtr<FNiagaraParameterAction>(ActionPtr.Pin());
	if (ParameterAction.IsValid())
	{
		int32 TotalCount = 0;
		for (const FNiagaraGraphParameterReferenceCollection& ReferenceCollection : ParameterAction->ReferenceCollection)
		{
			TotalCount += ReferenceCollection.ParameterReferences.Num();
		}
		return FText::AsNumber(TotalCount);
	}
	return FText::GetEmpty();
}

#undef LOCTEXT_NAMESPACE // "SNiagaraParameterMapPalleteItem"