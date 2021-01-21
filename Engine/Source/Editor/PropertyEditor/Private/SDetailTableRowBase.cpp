// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDetailTableRowBase.h"

int32 SDetailTableRowBase::GetIndentLevelForBackgroundColor() const
{
	int32 IndentLevel = 0; 
	if (OwnerTablePtr.IsValid())
	{
		// every item is in a category, but we don't want to show an indent for "top-level" properties
		IndentLevel = GetIndentLevel() - 1;
	}

	TSharedPtr<FDetailTreeNode> DetailTreeNode = OwnerTreeNode.Pin();
	if (DetailTreeNode.IsValid() && 
		DetailTreeNode->GetDetailsView() != nullptr && 
		DetailTreeNode->GetDetailsView()->ContainsMultipleTopLevelObjects())
	{
		// if the row is in a multiple top level object display (eg. Project Settings), don't display an indent for the initial level
		--IndentLevel;
	}

	return FMath::Max(0, IndentLevel);
}

FMargin SDetailTableRowBase::GetRowScrollBarPadding(TWeakPtr<STableViewBase> OwnerTableViewWeak) const
{
	TSharedPtr<STableViewBase> OwnerTableView = OwnerTableViewWeak.Pin();
	if (OwnerTableView.IsValid())
	{
		if (OwnerTableView->GetScrollbarVisibility() == EVisibility::Visible)
		{
			const float ScrollbarPaddingSize = 16.0f;
			return FMargin(0, 0, ScrollbarPaddingSize, 1);
		}
	}
	return FMargin(0, 0, 0, 1);
}
