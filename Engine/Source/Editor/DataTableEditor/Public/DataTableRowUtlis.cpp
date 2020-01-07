// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataTableRowUtlis.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Textures/SlateIcon.h"
#include "Widgets/SWidget.h"
#include "Framework/Commands/GenericCommands.h"
#include "IDataTableEditor.h"

#define LOCTEXT_NAMESPACE "FDataTableRowUtils"

void FDataTableRowUtils::AddSearchForReferencesContextMenu(FDetailWidgetRow& RowNameDetailWidget, FExecuteAction SearchForReferencesAction)
{
	if (SearchForReferencesAction.IsBound() && FEditorDelegates::OnOpenReferenceViewer.IsBound())
	{
		RowNameDetailWidget.AddCustomContextMenuAction(FUIAction(SearchForReferencesAction), 
			LOCTEXT("FDataTableRowUtils_SearchForReferences", "Find Row References"),
			LOCTEXT("FDataTableRowUtils_SearchForReferencesTooltip", "Find assets that reference this Row"),
			FSlateIcon());
	}
}

#undef  LOCTEXT_NAMESPACE