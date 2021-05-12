// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "Containers/Array.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "Misc/Guid.h"
#include "Widgets/Layout/SSplitter.h"

class SWidget;
class SRCPanelGroup;
struct SRCPanelExposedField;
struct SRCPanelExposedActor;

/**
 * Holds data about a row's columns' size.
 */
struct FRCColumnSizeData
{
	TAttribute<float> LeftColumnWidth;
	TAttribute<float> RightColumnWidth;
	SSplitter::FOnSlotResized OnWidthChanged;

	void SetColumnWidth(float InWidth) { OnWidthChanged.ExecuteIfBound(InWidth); }
};

/** A node in the panel tree view. */
struct SRCPanelTreeNode
{
	enum ENodeType
	{
		Invalid,
		Group,
		Field,
		FieldChild,
		Actor
	};

	virtual ~SRCPanelTreeNode() {}

	/** Get this tree node's childen. */
	virtual void GetNodeChildren(TArray<TSharedPtr<SRCPanelTreeNode>>& OutChildren) const {}
	/** Get this node's ID if any. */
	virtual FGuid GetId() const { return FGuid(); }
	/** Get get this node's type. */
	virtual ENodeType GetType() const { return Invalid; };
	/** Refresh the node. */
	virtual void Refresh() {};
	/** Get the context menu for this node. */
	virtual TSharedPtr<SWidget> GetContextMenu() { return nullptr; }

	//~ Utiliy methods for not having to downcast 
	virtual TSharedPtr<SRCPanelExposedField> AsField() { return nullptr; }
	virtual TSharedPtr<SWidget> AsFieldChild() { return nullptr; }
	virtual TSharedPtr<SRCPanelGroup> AsGroup() { return nullptr; }
	virtual TSharedPtr<SRCPanelExposedActor> AsActor() { return nullptr; }

protected:
	struct FMakeNodeWidgetArgs
	{
		TSharedPtr<SWidget> DragHandle;
		TSharedPtr<SWidget> NameWidget;
		TSharedPtr<SWidget> RenameButton;
		TSharedPtr<SWidget> ValueWidget;
		TSharedPtr<SWidget> UnexposeButton;
	};
	
	/** Create a widget that represents a row with a splitter. */
	TSharedRef<SWidget> MakeSplitRow(TSharedRef<SWidget> LeftColumn, TSharedRef<SWidget> RightColumn);
	/** Create a widget that represents a node in the panel tree hierarchy. */
	TSharedRef<SWidget> MakeNodeWidget(const FMakeNodeWidgetArgs& Args);

private:
	/** Stub handler for column resize callback to prevent the splitter from handling it internally.  */
	void OnLeftColumnResized(float) const;

	//~ Wrappers around ColumnSizeData's delegate needed in order to offset the splitter for RC Groups. 
	float GetLeftColumnWidth() const;
	float GetRightColumnWidth() const;
	void SetColumnWidth(float InWidth);

protected:
	/** Holds the row's columns' width. */
	FRCColumnSizeData ColumnSizeData;

private:
	/** The splitter offset to align the group splitter with the other row's splitters. */
	static constexpr float SplitterOffset = 0.008f;
};

class FExposedEntityDragDrop : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FExposedFieldDragDropOp, FDecoratedDragDropOp)

	using WidgetType = SWidget;

	FExposedEntityDragDrop(TSharedPtr<SWidget> InWidget, FGuid InId)
		: Id(MoveTemp(InId))
	{
		DecoratorWidget = SNew(SBorder)
			.Padding(1.0f)
			.BorderImage(FEditorStyle::GetBrush("DetailsView.CategoryMiddle_Active"))
			.Content()
			[
				InWidget.ToSharedRef()
			];

		Construct();
	}

	/** Get the ID of the represented entity or group. */
	FGuid GetId()
	{
		return Id;
	}

	virtual void OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent) override
	{
		FDecoratedDragDropOp::OnDrop(bDropWasHandled, MouseEvent);
	}

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override
	{
		return DecoratorWidget;
	}

private:
	FGuid Id;
	TSharedPtr<SWidget> DecoratorWidget;
};

#undef LOCTEXT_NAMESPACE /*RemoteControlPanelNode*/
