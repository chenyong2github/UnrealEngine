// Copyright Epic Games, Inc. All Rights Reserved.

#include "SOptimusEditorGraphExplorerItem.h"

#include "OptimusActionStack.h"
#include "OptimusEditor.h"
#include "OptimusEditorGraphSchema.h"
#include "OptimusNameValidator.h"

#include "IOptimusNodeGraphCollectionOwner.h"
#include "OptimusNodeGraph.h"

#include "Widgets/Text/SInlineEditableTextBlock.h"

void SOptimusEditorGraphEplorerItem::Construct(
	const FArguments& InArgs, 
	FCreateWidgetForActionData* const InCreateData, 
	TWeakPtr<FOptimusEditor> InOptimusEditor
	)
{
	TSharedPtr<FEdGraphSchemaAction> GraphAction = InCreateData->Action;
	ActionPtr = InCreateData->Action;
	OptimusEditor = InOptimusEditor;

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


bool SOptimusEditorGraphEplorerItem::OnNameTextVerifyChanged(
	const FText& InNewText, 
	FText& OutErrorMessage
	)
{
	TSharedPtr<FEdGraphSchemaAction> Action = ActionPtr.Pin();
	TSharedPtr<FOptimusEditor> Editor = OptimusEditor.Pin();

	if (ensure(Action) && ensure(Editor))
	{
		FString NameStr = InNewText.ToString();

		FName OriginalName;

		if (Action->GetTypeId() == FOptimusSchemaAction_Graph::StaticGetTypeId())
		{
			FOptimusSchemaAction_Graph *GraphAction = static_cast<FOptimusSchemaAction_Graph *>(Action.Get());
			UOptimusNodeGraph* NodeGraph = Editor->GetGraphCollectionRoot()->ResolveGraphPath(GraphAction->GraphPath);
			if (ensure(NodeGraph))
			{
				OriginalName = NodeGraph->GetFName();
			}
		}

		TSharedPtr<INameValidatorInterface> NameValidator = MakeShareable(new FOptimusNameValidator(Editor->GetGraphCollectionRoot(), OriginalName));

		EValidatorResult ValidatorResult = NameValidator->IsValid(NameStr);
		switch (ValidatorResult)
		{
		case EValidatorResult::Ok:
		case EValidatorResult::ExistingName:
			// These are fine, don't need to surface to the user, the rename can 'proceed' even if the name is the existing one
			break;
		default:
			OutErrorMessage = INameValidatorInterface::GetErrorText(NameStr, ValidatorResult);
			break;
		}

		return OutErrorMessage.IsEmpty();
	}
	else
	{
		return false;
	}
}


void SOptimusEditorGraphEplorerItem::OnNameTextCommitted(
	const FText& InNewText, 
	ETextCommit::Type InTextCommit
	)
{
	TSharedPtr<FEdGraphSchemaAction> Action = ActionPtr.Pin();
	TSharedPtr<FOptimusEditor> Editor = OptimusEditor.Pin();
	
	if (ensure(Action) && ensure(Editor))
	{
		FString NameStr = InNewText.ToString();

		FName OriginalName;

		if (Action->GetTypeId() == FOptimusSchemaAction_Graph::StaticGetTypeId())
		{
			FOptimusSchemaAction_Graph* GraphAction = static_cast<FOptimusSchemaAction_Graph*>(Action.Get());
			UOptimusNodeGraph* NodeGraph = Editor->GetGraphCollectionRoot()->ResolveGraphPath(GraphAction->GraphPath);

			if (ensure(NodeGraph))
			{
				NodeGraph->GetOwnerCollection()->RenameGraph(NodeGraph, NameStr);
			}
		}
	}
}
