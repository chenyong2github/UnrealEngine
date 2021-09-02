// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Views/SListView.h"

#include "Widgets/Views/STableRow.h"

class FDMXEntityDragDropOperation;
class FDMXEntityTreeCategoryNode;
class SDMXFixturePatchTree;
class UDMXLibrary;

class STableViewBase;


/** A row displaying a universe in a Fixture Patch Tree */
class SDMXFixturePatchTreeUniverseRow
	: public STableRow<TSharedPtr<FDMXEntityTreeCategoryNode>>
{
public:
	SLATE_BEGIN_ARGS(SDMXFixturePatchTreeUniverseRow)
	{}
		SLATE_DEFAULT_SLOT(typename SDMXFixturePatchTreeUniverseRow::FArguments, Content)

		SLATE_EVENT(FSimpleDelegate, OnFixturePatchOrderChanged)

	SLATE_END_ARGS()

	/** Constructs this widget */
	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, TSharedPtr<FDMXEntityTreeCategoryNode> InCategoryNode, bool bInIsRootCategory, TWeakPtr<SDMXFixturePatchTree> InEditorList);

	/** Sets an arbitrary widget as the content of the row */
	virtual void SetContent(TSharedRef<SWidget> InContent) override;

protected:
	//~ SWidget interface begin
	virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	//~ SWidget interface end

private:
	/** Returns true if the drag drop op can be dropped on this row */
	bool TestCanDropWithFeedback(const TSharedRef<FDMXEntityDragDropOperation>& EntityDragDropOp) const;

	/** Returns the categories nodes the entities were dragged out from */
	TArray<TSharedRef<FDMXEntityTreeCategoryNode>> GetCategoryNodesFromDragDropOp(const TSharedRef<FDMXEntityDragDropOperation>& DragDropOp) const;

	/** Returns the background image brush */
	const FSlateBrush* GetBackgroundImageBrush() const;

	/** Returns the DMXLibrary of this row */
	UDMXLibrary* GetDMXLibrary() const;

	/** Pointer to the node this row displays */
	TWeakPtr<FDMXEntityTreeCategoryNode> WeakCategoryNode;

	/** The owning Fixture Patch List */
	TWeakPtr<SDMXFixturePatchTree> WeakFixturePatchTree;

	/** Border that holds the content */
	TSharedPtr<SBorder> ContentBorder;

	/** Called when the entity list changed order of the library's entity array */
	FSimpleDelegate OnFixturePatchOrderChanged;
};
