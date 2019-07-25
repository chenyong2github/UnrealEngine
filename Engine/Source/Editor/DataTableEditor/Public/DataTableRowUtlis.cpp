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

const FText InsertNewRowActionName = LOCTEXT("FDataTableRowUtils_InsertNewRow", "Insert New Row");
const FText InsertNewRowActionTooltip = LOCTEXT("FDataTableRowUtils_InsertNewRowTooltip", "Insert a new Row");

TSharedRef<SWidget> FDataTableRowUtils::MakeRowActionsMenu(TSharedPtr<IDataTableEditor> Editor, FExecuteAction SearchForReferencesAction, FExecuteAction InsertNewRowAction)
{
	if (SearchForReferencesAction.IsBound())
	{
		FMenuBuilder MenuBuilder(true, Editor->GetToolkitCommands());
		MenuBuilder.AddMenuEntry(InsertNewRowActionName, InsertNewRowActionTooltip,
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Plus"), FUIAction(InsertNewRowAction));
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Copy);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Paste);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Duplicate);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Rename);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Delete);
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