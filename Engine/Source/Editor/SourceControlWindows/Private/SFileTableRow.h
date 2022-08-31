// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"

#include "SSourceControlCommon.h"

/** Display information about a file (icon, name, location, type, etc.) */
class SFileTableRow : public SMultiColumnTableRow<FChangelistTreeItemPtr>
{
public:
	SLATE_BEGIN_ARGS(SFileTableRow)
		: _TreeItemToVisualize()
	{}
	SLATE_ARGUMENT(FChangelistTreeItemPtr, TreeItemToVisualize)
		SLATE_EVENT(FOnDragDetected, OnDragDetected)
		SLATE_END_ARGS()

public:
	/**
	* Construct child widgets that comprise this widget.
	*
	* @param InArgs Declaration from which to construct this widget.
	*/
	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwner)
	{
		TreeItem = static_cast<FFileTreeItem*>(InArgs._TreeItemToVisualize.Get());

		FSuperRowType::FArguments Args = FSuperRowType::FArguments()
			.OnDragDetected(InArgs._OnDragDetected)
			.ShowSelection(true);
		FSuperRowType::Construct(Args, InOwner);
	}

	// SMultiColumnTableRow overrides
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		if (ColumnName == TEXT("Icon"))
		{
			return SNew(SBox)
				.WidthOverride(16) // Small Icons are usually 16x16
				.HAlign(HAlign_Center)
				[
					SSourceControlCommon::GetSCCFileWidget(TreeItem->FileState, TreeItem->IsShelved())
				];
		}
		else if (ColumnName == TEXT("Name"))
		{
			return SNew(STextBlock)
				.Text(this, &SFileTableRow::GetDisplayName);
		}
		else if (ColumnName == TEXT("Path"))
		{
			return SNew(STextBlock)
				.Text(this, &SFileTableRow::GetDisplayPath)
				.ToolTipText(this, &SFileTableRow::GetFilename);
		}
		else if (ColumnName == TEXT("Type"))
		{
			return SNew(STextBlock)
				.Text(this, &SFileTableRow::GetDisplayType)
				.ColorAndOpacity(this, &SFileTableRow::GetDisplayColor);
		}
		else
		{
			return SNullWidget::NullWidget;
		}
	}

	FText GetDisplayName() const
	{
		return TreeItem->GetAssetName();
	}

	FText GetFilename() const
	{
		return TreeItem->GetFileName();
	}

	FText GetDisplayPath() const
	{
		return TreeItem->GetAssetPath();
	}

	FText GetDisplayType() const
	{
		return TreeItem->GetAssetType();
	}

	FSlateColor GetDisplayColor() const
	{
		return TreeItem->GetAssetTypeColor();
	}

protected:
	//~ Begin STableRow Interface.
	virtual void OnDragEnter(FGeometry const& InGeometry, FDragDropEvent const& InDragDropEvent) override
	{
		TSharedPtr<FDragDropOperation> DragOperation = InDragDropEvent.GetOperation();
		DragOperation->SetCursorOverride(EMouseCursor::SlashedCircle);
	}

	virtual void OnDragLeave(FDragDropEvent const& InDragDropEvent) override
	{
		TSharedPtr<FDragDropOperation> DragOperation = InDragDropEvent.GetOperation();
		DragOperation->SetCursorOverride(EMouseCursor::None);
	}
	//~ End STableRow Interface.

private:
	/** The info about the widget that we are visualizing. */
	FFileTreeItem* TreeItem;
};
