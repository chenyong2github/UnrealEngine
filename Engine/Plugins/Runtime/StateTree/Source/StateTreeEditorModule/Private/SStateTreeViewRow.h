// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeState.h"
#include "Widgets/Views/STableRow.h"

class UStateTreeEditorData;
class SStateTreeView;
class SInlineEditableTextBlock;
class SScrollBox;
class FStateTreeViewModel;

class SStateTreeViewRow : public STableRow<UStateTreeState*>
{
public:

	SLATE_BEGIN_ARGS(SStateTreeViewRow)
	{
	}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, UStateTreeState* InState, const TSharedPtr<SScrollBox>& ViewBox, TSharedRef<FStateTreeViewModel> InStateTreeViewModel);
	void RequestRename();

private:

	FSlateColor GetTitleColor() const;
	FText GetStateDesc() const;

	EVisibility GetConditionVisibility() const;
	EVisibility GetSelectorVisibility() const;
	FText GetSelectorDesc() const;

	EVisibility GetTasksVisibility() const;
	FText GetTasksDesc() const;

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

	FText GetTransitionsDesc(const UStateTreeState& State, const EStateTreeTransitionEvent Event) const;
	FText GetTransitionsIcon(const UStateTreeState& State, const EStateTreeTransitionEvent Event) const;
	EVisibility GetTransitionsVisibility(const UStateTreeState& State, const EStateTreeTransitionEvent Event) const;

	bool HasParentTransitionForEvent(const UStateTreeState& State, const EStateTreeTransitionEvent Event) const;

	bool IsRootState() const;
	bool IsSelected() const;

	bool VerifyNodeTextChanged(const FText& NewLabel, FText& OutErrorMessage);
	void HandleNodeLabelTextCommitted(const FText& NewLabel, ETextCommit::Type CommitType);

	FReply HandleDragDetected(const FGeometry&, const FPointerEvent&);
	TOptional<EItemDropZone> HandleCanAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, UStateTreeState* TargetState);
	FReply HandleAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, UStateTreeState* TargetState);
	
	TSharedPtr<FStateTreeViewModel> StateTreeViewModel;
	TWeakObjectPtr<UStateTreeState> WeakState;
	TSharedPtr<SInlineEditableTextBlock> NameTextBlock;
};
