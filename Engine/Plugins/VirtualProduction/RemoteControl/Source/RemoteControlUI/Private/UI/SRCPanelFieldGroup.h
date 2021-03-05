// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "Input/DragAndDrop.h"
#include "Misc/Guid.h"
#include "SRCPanelTreeNode.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "UObject/WeakObjectPtrTemplates.h"

class SRemoteControlPanel;
struct SRCPanelTreeNode;
class STableViewBase;
struct FSlateColor;
struct FSlateBrush;
class SInlineEditableTextBlock;
class URemoteControlPreset;

/**
 * Holds information about a group which contains exposed entities.
 */
struct FRCPanelGroup : public SRCPanelTreeNode, public TSharedFromThis<FRCPanelGroup>
{
	FRCPanelGroup(FName InName, FGuid InId)
		: Name(InName)
		, Id(InId)
	{}

	FRCPanelGroup(FName InName, FGuid InId, TArray<TSharedPtr<SRCPanelTreeNode>> InNodes)
		: Name(InName)
		, Id(InId)
		, Nodes(MoveTemp(InNodes))
	{}

	//~ SRCPanelTreeNode Interface
	virtual void GetNodeChildren(TArray<TSharedPtr<SRCPanelTreeNode>>& OutChildren) const override;
	virtual FGuid GetId() const override;
	virtual ENodeType GetType() const override;
	virtual TSharedPtr<FRCPanelGroup> AsGroup() override;

public:
	/** Name of the group. */
	FName Name;
	/** Id for this group. (Matches the one in the preset layout data. */
	FGuid Id;
	/** This group's child nodes */
	TArray<TSharedPtr<SRCPanelTreeNode>> Nodes;
};

/** Widget representing a group. */
class SFieldGroup : public STableRow<TSharedPtr<FRCPanelGroup>>
{
public:
	DECLARE_DELEGATE_RetVal_ThreeParams(FReply, FOnFieldDropEvent, const TSharedPtr<FDragDropOperation>& /* Event */, const TSharedPtr<SRCPanelTreeNode>& /* TargetField */, const TSharedPtr<FRCPanelGroup>& /* DragTargetGroup */);
	DECLARE_DELEGATE_RetVal_OneParam(FGuid, FOnGetGroupId, const FGuid& /* EntityId */);
	DECLARE_DELEGATE_OneParam(FOnDeleteGroup, const TSharedPtr<FRCPanelGroup>&);

	SLATE_BEGIN_ARGS(SFieldGroup)
		: _EditMode(true)
	{}
		SLATE_EVENT(FOnFieldDropEvent, OnFieldDropEvent)
		SLATE_EVENT(FOnGetGroupId, OnGetGroupId)
		SLATE_EVENT(FOnDeleteGroup, OnDeleteGroup)
		SLATE_ATTRIBUTE(bool, EditMode)
	SLATE_END_ARGS()

	void Tick(const FGeometry&, const double, const float);
	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, const TSharedPtr<FRCPanelGroup>& InFieldGroup, URemoteControlPreset* InPreset);

	/** Refresh this groups' node list. */
	void Refresh();

	/** Get this group's name. */
	FName GetGroupName() const;
	/** Get this widget's underlying group. */
	TSharedPtr<FRCPanelGroup> GetGroup() const;

	/** Set this widget's name. */
	void SetName(FName Name);

private:
	//~ Handle drag/drop events
	FReply OnFieldDropGroup(const FDragDropEvent& Event, TSharedPtr<SRCPanelTreeNode> TargetField);
	FReply OnFieldDropGroup(TSharedPtr<FDragDropOperation> DragDropOperation, TSharedPtr<SRCPanelTreeNode> TargetField);
	bool OnAllowDropFromOtherGroup(TSharedPtr<FDragDropOperation> DragDropOperation);

	/** Handles group deletion */
	FReply HandleDeleteGroup();
	/** Returns group name's text color according to the current selection. */
	FSlateColor GetGroupNameTextColor() const;
	/** Get the border image according to the current selection. */
	const FSlateBrush* GetBorderImage() const;
	/** Get the visibility according to the panel's current mode. */
	EVisibility GetVisibilityAccordingToEditMode(EVisibility DefaultHiddenVisibility) const;

	bool OnVerifyItemLabelChanged(const FText& InLabel, FText& OutErrorMessage);

	void OnLabelCommitted(const FText& InLabel, ETextCommit::Type InCommitInfo);

private:
	/** Holds the list view widget. */
	TSharedPtr<SListView<TSharedPtr<SRCPanelTreeNode>>> NodesListView;
	/** The field group that interfaces with the underlying data. */
	TSharedPtr<FRCPanelGroup> FieldGroup;
	/** Event called when something is dropped on this group. */
	FOnFieldDropEvent OnFieldDropEvent;
	/** Getter for this group's name. */
	FOnGetGroupId OnGetGroupId;
	/** Event called then the user deletes the group. */
	FOnDeleteGroup OnDeleteGroup;
	/** Holds the text box for the group name. */
	TSharedPtr<SInlineEditableTextBlock> NameTextBox;
	/** Whether the panel is currently in edit mode. */
	TAttribute<bool> bEditMode;
	/** Whether the group needs to be renamed. (As requested by a click on the rename button) */
	bool bNeedsRename = false;
	/** Weak ptr to the preset that contains the field group. */
	TWeakObjectPtr<URemoteControlPreset> Preset;
};


class FFieldGroupDragDropOp : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FFieldGroupDragDropOp, FDragDropOperation)

	using WidgetType = SFieldGroup;

	FFieldGroupDragDropOp(TSharedPtr<SFieldGroup> InWidget, FGuid InId)
		: Id(MoveTemp(InId))
	{
		DecoratorWidget = SNew(SBorder)
			.Padding(0.f)
			.BorderImage(FEditorStyle::GetBrush("Graph.ConnectorFeedback.Border"))
			.Content()
			[
				InWidget.ToSharedRef()
			];

		Construct();
	}

	FGuid GetGroupId() const
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
	/** Id of the held group. */
	FGuid Id;

	/** Holds the displayed widget. */
	TSharedPtr<SWidget> DecoratorWidget;
};

