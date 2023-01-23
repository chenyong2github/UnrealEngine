// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/List/RowTypes/ObjectMixerEditorListRowFolder.h"

#include "Views/List/SObjectMixerEditorList.h"

const FSceneOutlinerTreeItemType FObjectMixerEditorListRowFolder::Type(&FFolderTreeItem::Type);

void FObjectMixerEditorListRowFolder::OnVisibilityChanged(const bool bNewVisibility)
{
	RowData.OnChangeVisibility(SharedThis(this), bNewVisibility);
								
	RowData.GetListView()->EvaluateAndSetEditorVisibilityPerRow();
}
