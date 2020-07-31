// Copyright Epic Games, Inc. All Rights Reserved.

#include "SOptimusEditorGraphExplorerItem.h"

#include "OptimusEditor.h"
#include "OptimusEditorGraphSchema.h"

#include "Widgets/Text/SInlineEditableTextBlock.h"

void SOptimusEditorGraphEplorerItem::Construct(
	const FArguments& InArgs, 
	FCreateWidgetForActionData* const InCreateData, 
	TWeakPtr<FOptimusEditor> InOptimusEditor
	)
{
	TSharedPtr<FEdGraphSchemaAction> GraphAction = InCreateData->Action;
	ActionPtr = InCreateData->Action;

	TWeakPtr<FEdGraphSchemaAction> WeakGraphAction = GraphAction;
	const bool bIsReadOnlyCreate = InCreateData->bIsReadOnly;
	auto IsReadOnlyLambda = [WeakGraphAction, InOptimusEditor, bIsReadOnlyCreate]()
	{
		if (WeakGraphAction.IsValid() && InOptimusEditor.IsValid())
		{
		}

		return bIsReadOnlyCreate;
	};
	TAttribute<bool> bIsReadOnly = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateLambda(IsReadOnlyLambda));

	FSlateFontInfo NameFont = FCoreStyle::GetDefaultFontStyle("Regular", 10);

	ChildSlot
	[
		CreateTextSlotWidget( NameFont, InCreateData, bIsReadOnly )
	];
}


TSharedRef<SWidget> SOptimusEditorGraphEplorerItem::CreateTextSlotWidget(const FSlateFontInfo& NameFont, FCreateWidgetForActionData* const InCreateData, TAttribute<bool> InbIsReadOnly)
{
	FOnVerifyTextChanged OnVerifyTextChanged;
	FOnTextCommitted OnTextCommitted;

	if (false /* Check for specific action rename options */)
	{

	}
	else
	{
		OnVerifyTextChanged.BindSP(this, &SOptimusEditorGraphEplorerItem::OnNameTextVerifyChanged);
		OnTextCommitted.BindSP(this, &SOptimusEditorGraphEplorerItem::OnNameTextCommitted);
	}

	if (InCreateData->bHandleMouseButtonDown)
	{
		MouseButtonDownDelegate = InCreateData->MouseButtonDownDelegate;
	}

	// FIXME: Tooltips

	TSharedPtr<SInlineEditableTextBlock> EditableTextElement = SNew(SInlineEditableTextBlock)
	    .Text(this, &SOptimusEditorGraphEplorerItem::GetDisplayText)
	    .Font(NameFont)
	    .HighlightText(InCreateData->HighlightText)
	    // .ToolTip(ToolTipWidget)
	    .OnVerifyTextChanged(OnVerifyTextChanged)
	    .OnTextCommitted(OnTextCommitted)
	    .IsSelected(InCreateData->IsRowSelectedDelegate)
	    .IsReadOnly(InbIsReadOnly);

	InlineRenameWidget = EditableTextElement.ToSharedRef();

	InCreateData->OnRenameRequest->BindSP(InlineRenameWidget.Get(), &SInlineEditableTextBlock::EnterEditingMode);

	return InlineRenameWidget.ToSharedRef();
}


FText SOptimusEditorGraphEplorerItem::GetDisplayText() const
{
	const UOptimusEditorGraphSchema* Schema = GetDefault<UOptimusEditorGraphSchema>();
	if (MenuDescriptionCache.IsOutOfDate(Schema))
	{
		TSharedPtr< FEdGraphSchemaAction > GraphAction = ActionPtr.Pin();

		MenuDescriptionCache.SetCachedText(ActionPtr.Pin()->GetMenuDescription(), Schema);
	}

	return MenuDescriptionCache;
}


bool SOptimusEditorGraphEplorerItem::OnNameTextVerifyChanged(const FText& InNewText, FText& OutErrorMessage)
{
	return false;
}


void SOptimusEditorGraphEplorerItem::OnNameTextCommitted(const FText& NewText, ETextCommit::Type InTextCommit)
{
}


