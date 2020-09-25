// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/TreeViews/DisplayClusterConfiguratorTreeItem.h"

#include "DisplayClusterConfiguratorStyle.h"
#include "DisplayClusterConfiguratorToolkit.h"
#include "Interfaces/Views/TreeViews/IDisplayClusterConfiguratorViewTree.h"
#include "Views/TreeViews/SDisplayClusterConfiguratorTreeItemRow.h"

#include "UObject/Object.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Views/STableViewBase.h"

TSharedRef<ITableRow> FDisplayClusterConfiguratorTreeItem::MakeTreeRowWidget(const TSharedRef<STableViewBase>& InOwnerTable, const TAttribute<FText>& InFilterText)
{
	return SNew(SDisplayClusterConfiguratorTreeItemRow, InOwnerTable)
		.FilterText(InFilterText)
		.Item(SharedThis(this));
}

void FDisplayClusterConfiguratorTreeItem::GenerateWidgetForItemColumn(TSharedPtr<SHorizontalBox> Box, const TAttribute<FText>& FilterText, FIsSelected InIsSelected)
{
	Box->AddSlot()
		.AutoWidth()
		.Padding(FMargin(0.0f, 1.0f))
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Center)
		[
			SNew(SImage)
			.ColorAndOpacity(FSlateColor::UseForeground())
			.Image(FDisplayClusterConfiguratorStyle::GetBrush(*GetIconStyle()))
		];

	Box->AddSlot()
		.AutoWidth()
		.Padding(2, 0, 0, 0)
		.VAlign(VAlign_Center)
		[
			SNew( STextBlock )
			.Text( FText::FromName(GetRowItemName()) )
			.HighlightText( FilterText )
		];
}

void FDisplayClusterConfiguratorTreeItem::GetChildrenObjectsRecursive(TArray<UObject*>& OutObjects) const
{
	const TArray<TSharedPtr<IDisplayClusterConfiguratorTreeItem>>& ItemChildren = GetChildrenConst();
	for (const TSharedPtr<IDisplayClusterConfiguratorTreeItem>& TreeItem : ItemChildren)
	{
		OutObjects.Add(TreeItem->GetObject());
		TreeItem->GetChildrenObjectsRecursive(OutObjects);
	}
}

bool FDisplayClusterConfiguratorTreeItem::IsChildOfRecursive(const TSharedRef<IDisplayClusterConfiguratorTreeItem>& InTreeItem) const
{
	if (TSharedPtr<IDisplayClusterConfiguratorTreeItem> ParentItem = GetParent())
	{
		if (ParentItem->GetRowItemName() == InTreeItem->GetRowItemName())
		{
			return true;
		}
		return ParentItem->IsChildOfRecursive(InTreeItem);
	}

	return false;
}

void FDisplayClusterConfiguratorTreeItem::OnSelection()
{
	TArray<UObject*> SelectedObjects;
	SelectedObjects.Add(GetObject());

	ToolkitPtr.Pin()->SelectObjects(SelectedObjects);
}

bool FDisplayClusterConfiguratorTreeItem::IsSelected()
{
	const TArray<UObject*>& SelectedObjects = ToolkitPtr.Pin()->GetSelectedObjects();

	UObject* const* SelectedObject = SelectedObjects.FindByPredicate([this](const UObject* InObject)
	{
		return InObject == GetObject();
	});

	if (SelectedObject != nullptr)
	{
		UObject* Obj = *SelectedObject;

		return Obj != nullptr;
	}

	return false;
}
