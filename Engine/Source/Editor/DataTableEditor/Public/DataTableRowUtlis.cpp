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

TSharedRef<SWidget> FDataTableRowUtils::MakeRowActionsMenu(TSharedPtr<IDataTableEditor> Editor, FExecuteAction SearchForReferencesAction)
{
	if (SearchForReferencesAction.IsBound())
	{
		FMenuBuilder MenuBuilder(true, Editor->GetToolkitCommands());
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Copy);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Paste);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Duplicate);
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