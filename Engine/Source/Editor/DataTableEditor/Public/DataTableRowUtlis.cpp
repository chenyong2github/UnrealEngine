// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataTableRowUtlis.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Textures/SlateIcon.h"
#include "Widgets/SWidget.h"
#include "Framework/Commands/GenericCommands.h"
#include "IDataTableEditor.h"

#define LOCTEXT_NAMESPACE "FDataTableRowUtils"

const FText FDataTableRowUtils::SearchForReferencesActionName = LOCTEXT("FDataTableRowUtils_SearchForReferences", "Find Row References");
const FText FDataTableRowUtils::SearchForReferencesActionTooltip = LOCTEXT("FDataTableRowUtils_SearchForReferencesTooltip", "Find assets that reference this Row");

const FText FDataTableRowUtils::InsertNewRowActionName = LOCTEXT("FDataTableRowUtils_InsertNewRow", "Insert New Row");
const FText FDataTableRowUtils::InsertNewRowActionTooltip = LOCTEXT("FDataTableRowUtils_InsertNewRowTooltip", "Insert a new Row");

const FText FDataTableRowUtils::InsertNewRowAboveActionName = LOCTEXT("FDataTableRowUtils_InsertNewRowAbove", "Insert New Row Above");
const FText FDataTableRowUtils::InsertNewRowAboveActionTooltip = LOCTEXT("FDataTableRowUtils_InsertNewRowAboveTooltip", "Insert a new Row above the current selection");

const FText FDataTableRowUtils::InsertNewRowBelowActionName = LOCTEXT("FDataTableRowUtils_InsertNewRowBelow", "Insert New Row Below");
const FText FDataTableRowUtils::InsertNewRowBelowActionTooltip = LOCTEXT("FDataTableRowUtils_InsertNewRowBelowTooltip", "Insert a new Row below the current selection");

const FText FDataTableRowUtils::MoveToBottomActionName = LOCTEXT("FDataTableRowUtils_MoveToBottom", "Move Row To Bottom");
const FText FDataTableRowUtils::MoveToBottomActionTooltip = LOCTEXT("FDataTableRowUtils_MoveToBottomTooltip", "Move selected Row to the bottom");

const FText FDataTableRowUtils::MoveToTopActionName = LOCTEXT("FDataTableRowUtils_MoveToTopAction", "Move Row to Top");
const FText FDataTableRowUtils::MoveToTopActionTooltip = LOCTEXT("FDataTableRowUtils_MoveToTopActionTooltip", "Move selected Row to the top");


TSharedRef<SWidget> FDataTableRowUtils::MakeRowActionsMenu(TSharedPtr<IDataTableEditor> Editor, FExecuteAction SearchForReferencesAction, FExecuteAction InsertNewRowAction,
	FExecuteAction InsertNewRowAboveAction, FExecuteAction InsertNewRowBelowAction, FExecuteAction MoveToBottomAction, FExecuteAction MoveToTopAction)
{
	if (SearchForReferencesAction.IsBound() && InsertNewRowAction.IsBound() && InsertNewRowAboveAction.IsBound() 
		&& InsertNewRowBelowAction.IsBound() )
	{
		FMenuBuilder MenuBuilder(true, Editor->GetToolkitCommands());
		MenuBuilder.AddMenuEntry(InsertNewRowActionName, InsertNewRowActionTooltip,
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Plus"), FUIAction(InsertNewRowAction));
		MenuBuilder.AddMenuEntry(InsertNewRowAboveActionName, InsertNewRowAboveActionTooltip,
			FSlateIcon(), FUIAction(InsertNewRowAboveAction));
		MenuBuilder.AddMenuEntry(InsertNewRowBelowActionName, InsertNewRowBelowActionTooltip,
			FSlateIcon(), FUIAction(InsertNewRowBelowAction));

		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Copy);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Paste);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Duplicate);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Rename);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Delete);
		MenuBuilder.AddMenuSeparator();

		MenuBuilder.AddMenuEntry(MoveToTopActionName, MoveToTopActionTooltip,
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Symbols.DoubleUpArrow"), FUIAction(MoveToTopAction));
		MenuBuilder.AddMenuEntry(MoveToBottomActionName, MoveToBottomActionTooltip,
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Symbols.DoubleDownArrow"), FUIAction(MoveToBottomAction));
		MenuBuilder.AddMenuSeparator();
		
		MenuBuilder.AddMenuEntry(SearchForReferencesActionName, SearchForReferencesActionTooltip, 
			FSlateIcon(), FUIAction(SearchForReferencesAction));
		return MenuBuilder.MakeWidget();
	}

	return SNullWidget::NullWidget;
}

void FDataTableRowUtils::AddSearchForReferencesContextMenu(FDetailWidgetRow& RowNameDetailWidget, FExecuteAction SearchForReferencesAction)
{
	if (SearchForReferencesAction.IsBound() && FEditorDelegates::OnOpenReferenceViewer.IsBound())
	{
		RowNameDetailWidget.AddCustomContextMenuAction(FUIAction(SearchForReferencesAction), SearchForReferencesActionName, 
			SearchForReferencesActionTooltip, FSlateIcon());
	}
}

#undef  LOCTEXT_NAMESPACE