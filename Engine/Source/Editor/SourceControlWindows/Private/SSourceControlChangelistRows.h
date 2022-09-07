// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STreeView.h"
#include "Input/DragAndDrop.h"
#include "SSourceControlCommon.h"

/** Lists the unique column IDs used in the list view displaying controlled/uncontrolled changelist files. */
namespace SourceControlFileViewColumnId
{
	/** The icon column id. */
	extern const FName Icon;
	/** The file/asset name column id. */
	extern const FName Name;
	/** The file/asset path column id. */
	extern const FName Path;
	/** The file/asset type column Id. */
	extern const FName Type;
}


/** Displays a changed list row (icon, cl number, description) */
class SChangelistTableRow : public STableRow<TSharedPtr<IChangelistTreeItem>>
{
public:
	SLATE_BEGIN_ARGS(SChangelistTableRow)
		: _TreeItemToVisualize()
		, _OnPostDrop()
	{
	}
		SLATE_ARGUMENT(FChangelistTreeItemPtr, TreeItemToVisualize)
		SLATE_EVENT(FSimpleDelegate, OnPostDrop)
	SLATE_END_ARGS()

public:
	/**
	* Construct child widgets that comprise this widget.
	*
	* @param InArgs Declaration from which to construct this widget.
	*/
	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwner);

	FText GetChangelistText() const;
	FText GetChangelistDescriptionText() const;

protected:
	//~ Begin STableRow Interface.
	virtual FReply OnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent) override;

private:
	/** The info about the widget that we are visualizing. */
	FChangelistTreeItem* TreeItem;

	/** Delegate invoked once the drag and drop operation finished. */
	FSimpleDelegate OnPostDrop;
};


/** Displays an uncontrolled changed list (icon, cl name, description) */
class SUncontrolledChangelistTableRow : public STableRow<FChangelistTreeItemPtr>
{
public:
	SLATE_BEGIN_ARGS(SUncontrolledChangelistTableRow)
		: _TreeItemToVisualize()
		, _OnPostDrop()
	{
	}
		SLATE_ARGUMENT(FChangelistTreeItemPtr, TreeItemToVisualize)
		SLATE_EVENT(FSimpleDelegate, OnPostDrop)
	SLATE_END_ARGS()

public:
	/**
	* Construct child widgets that comprise this widget.
	*
	* @param InArgs Declaration from which to construct this widget.
	*/
	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwner);

	FText GetChangelistText() const;
	FText GetChangelistDescriptionText() const;

protected:
	//~ Begin STableRow Interface.
	virtual FReply OnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent) override;

private:
	/** The info about the widget that we are visualizing. */
	FUncontrolledChangelistTreeItem* TreeItem;

	/** Invoked once a drag and drop operation completes. */
	FSimpleDelegate OnPostDrop;
};


/** Display information about a file (icon, name, location, type, etc.) */
class SFileTableRow : public SMultiColumnTableRow<FChangelistTreeItemPtr>
{
public:
	SLATE_BEGIN_ARGS(SFileTableRow)
		: _TreeItemToVisualize()
	{
	}
		SLATE_ARGUMENT(FChangelistTreeItemPtr, TreeItemToVisualize)
		SLATE_EVENT(FOnDragDetected, OnDragDetected)
	SLATE_END_ARGS()

public:
	/**
	* Construct child widgets that comprise this widget.
	*
	* @param InArgs Declaration from which to construct this widget.
	*/
	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwner);

	// SMultiColumnTableRow overrides
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override;

	FText GetDisplayName() const;
	FText GetFilename() const;
	FText GetDisplayPath() const;
	FText GetDisplayType() const;
	FSlateColor GetDisplayColor() const;

protected:
	//~ Begin STableRow Interface.
	virtual void OnDragEnter(FGeometry const& InGeometry, FDragDropEvent const& InDragDropEvent) override;
	virtual void OnDragLeave(FDragDropEvent const& InDragDropEvent) override;
	//~ End STableRow Interface.

private:
	/** The info about the widget that we are visualizing. */
	FFileTreeItem* TreeItem;
};


/** Display the shelved files group node. It displays 'Shelved Files (x)' where X is the nubmer of file shelved. */
class SShelvedFilesTableRow : public STableRow<TSharedPtr<IChangelistTreeItem>>
{
public:
	SLATE_BEGIN_ARGS(SShelvedFilesTableRow)
		: _Icon(nullptr)
	{
	}
		SLATE_ARGUMENT(const FSlateBrush*, Icon)
		SLATE_ARGUMENT(FText, Text)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView);
};


/** Display information about an offline file (icon, name, location, type, etc.). */
class SOfflineFileTableRow : public SMultiColumnTableRow<FChangelistTreeItemPtr>
{
public:
	SLATE_BEGIN_ARGS(SOfflineFileTableRow)
		: _TreeItemToVisualize()
	{
	}
		SLATE_ARGUMENT(FChangelistTreeItemPtr, TreeItemToVisualize)
	SLATE_END_ARGS()

public:
	/**
	* Construct child widgets that comprise this widget.
	*
	* @param InArgs Declaration from which to construct this widget.
	*/
	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwner);

	// SMultiColumnTableRow overrides
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override;

	FText GetDisplayName() const;
	FText GetFilename() const;
	FText GetDisplayPath() const;
	FText GetDisplayType() const;
	FSlateColor GetDisplayColor() const;

private:
	/** The info about the widget that we are visualizing. */
	FOfflineFileTreeItem* TreeItem;
};
