// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/SListView.h"

class STableViewBase;
class UStateTreeState;
enum class EStateTreeTransitionTrigger : uint8;
struct FStateTreeStateLink;

class UStateTreeEditorData;
class SStateTreeView;
class SInlineEditableTextBlock;
class SScrollBox;
class FStateTreeViewModel;

class SStateTreeViewRow : public STableRow<TWeakObjectPtr<UStateTreeState>>
{
public:

	SLATE_BEGIN_ARGS(SStateTreeViewRow)
	{
	}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, TWeakObjectPtr<UStateTreeState> InState, const TSharedPtr<SScrollBox>& ViewBox, TSharedRef<FStateTreeViewModel> InStateTreeViewModel);
	void RequestRename() const;

private:
	TSharedRef<SHorizontalBox> CreateTasksWidget();

	FSlateColor GetTitleColor() const;
	FSlateColor GetActiveStateColor() const;
	FSlateColor GetSubTreeMarkerColor() const;
	FText GetStateDesc() const;

	EVisibility GetConditionVisibility() const;
	EVisibility GetStateBreakpointVisibility() const;
	FText GetStateBreakpointTooltipText() const;
	
	const FSlateBrush* GetSelectorIcon() const;
	FText GetSelectorTooltip() const;
	FText GetStateTypeTooltip() const;

	EVisibility GetTasksVisibility() const;

	EVisibility GetLinkedStateVisibility() const;
	FText GetLinkedStateDesc() const;

	EVisibility GetCompletedTransitionVisibility() const;
	FText GetCompletedTransitionsDesc() const;
	FText GetCompletedTransitionsIcon() const;

	EVisibility GetSucceededTransitionVisibility() const;
	FText GetSucceededTransitionDesc() const;
	FText GetSucceededTransitionIcon() const;

	EVisibility GetFailedTransitionVisibility() const;
	FText GetFailedTransitionDesc() const;
	FText GetFailedTransitionIcon() const;

	EVisibility GetConditionalTransitionsVisibility() const;
	FText GetConditionalTransitionsDesc() const;

	static FText GetLinkDescription(const FStateTreeStateLink& Link);
	FText GetTransitionsDesc(const UStateTreeState& State, const EStateTreeTransitionTrigger Trigger, const bool bUseMask = false) const;
	FText GetTransitionsIcon(const UStateTreeState& State, const EStateTreeTransitionTrigger Trigger, const bool bUseMask = false) const;
	EVisibility GetTransitionsVisibility(const UStateTreeState& State, const EStateTreeTransitionTrigger Trigger) const;

	bool HasParentTransitionForTrigger(const UStateTreeState& State, const EStateTreeTransitionTrigger Trigger) const;

	bool IsRootState() const;
	bool IsStateSelected() const;

	void HandleNodeLabelTextCommitted(const FText& NewLabel, ETextCommit::Type CommitType) const;

	FReply HandleDragDetected(const FGeometry&, const FPointerEvent&) const;
	TOptional<EItemDropZone> HandleCanAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, TWeakObjectPtr<UStateTreeState> TargetState) const;
	FReply HandleAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, TWeakObjectPtr<UStateTreeState> TargetState) const;
	
	TSharedPtr<FStateTreeViewModel> StateTreeViewModel;
	TWeakObjectPtr<UStateTreeState> WeakState;
	TWeakObjectPtr<UStateTreeEditorData> WeakTreeData;
	TSharedPtr<SInlineEditableTextBlock> NameTextBlock;
};
