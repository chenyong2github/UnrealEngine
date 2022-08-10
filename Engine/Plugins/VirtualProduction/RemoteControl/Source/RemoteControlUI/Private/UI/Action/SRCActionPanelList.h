// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UI/BaseLogicUI/SRCLogicPanelListBase.h"

struct FRCPanelStyle;
class FRCBehaviourModel;
class ActionType;
class ITableRow;
class STableViewBase;
class SRCActionPanel;
class URCAction;
class URemoteControlPreset;
template <typename ItemType> class SListView;

/*
* ~ SRCActionPanelList ~
*
* UI Widget for Actions List
* Used as part of the RC Logic Actions Panel.
*/
template <typename ActionType>
class REMOTECONTROLUI_API SRCActionPanelList : public SRCLogicPanelListBase
{
public:
	SLATE_BEGIN_ARGS(SRCActionPanelList<ActionType>)
		{
		}

	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs, const TSharedRef<SRCActionPanel> InActionPanel, TSharedPtr<FRCBehaviourModel> InBehaviourItem);

	/** Returns true if the underlying list is valid and empty. */
	virtual bool IsEmpty() const override;

	/** Returns number of items in the list. */
	virtual int32 Num() const override;

	/** Whether the Actions List View currently has focus.*/
	virtual bool IsListFocused() const override;

	/** Deletes currently selected items from the list view*/
	virtual void DeleteSelectedPanelItem() override;

	/** Fetches the parent Action panel*/
	TSharedPtr<SRCActionPanel> GetActionPanel()
	{
		return ActionPanelWeakPtr.Pin();
	}

	/** Fetches the Behaviour (UI model) associated with us */
	TSharedPtr<FRCBehaviourModel> GetBehaviourItem()
	{
		return BehaviourItemWeakPtr.Pin();
	}

	/** Adds an Action by Remote Control Field Guid*/
	URCAction* AddAction(const FGuid& InRemoteControlFieldId);

private:

	/** OnGenerateRow delegate for the Actions List View*/
	TSharedRef<ITableRow> OnGenerateWidgetForList( TSharedPtr<ActionType> InItem, const TSharedRef<STableViewBase>& OwnerTable );

	/** Responds to the selection of a newly created action. Resets UI state*/
	void OnActionAdded(URCAction* InAction);

	/** Responds to the removal of all actions. Rests UI state*/
	void OnEmptyActions();

	/** Refreshes the list from the latest state of the data model*/
	virtual void Reset() override;

	/** Handles broadcasting of a successful remove item operation.*/
	virtual void BroadcastOnItemRemoved() override {}

	/** Fetches the Remote Control preset associated with the parent panel */
	virtual URemoteControlPreset* GetPreset() override;

	/** Removes the given Action UI model item from the list of UI models*/
	virtual int32 RemoveModel(const TSharedPtr<FRCLogicModeBase> InModel) override;

	/*Drag and drop event for creating an Action from an exposed field */
	FReply OnExposedFieldDrop(TSharedPtr<FDragDropOperation> DragDropOperation);

	/** Whether drag and drop is possible from the current exposed property to the Actions table */
	bool OnAllowDrop(TSharedPtr<FDragDropOperation> DragDropOperation);

	/** Context menu for Actions panel list */
	TSharedPtr<SWidget> GetContextMenuWidget();

	/** OnSelectionChanged delegate for Actions List View */
	void OnSelectionChanged(TSharedPtr<ActionType> InItem, ESelectInfo::Type);

private:

	/** The currently selected Action item*/
	TSharedPtr<ActionType> SelectedActionItem;

	/** The parent Action Panel widget*/
	TWeakPtr<SRCActionPanel> ActionPanelWeakPtr;

	/** The Behaviour (UI model) associated with us*/
	TWeakPtr<FRCBehaviourModel> BehaviourItemWeakPtr;

	/** List of Actions (UI model) active in this widget */
	TArray<TSharedPtr<ActionType>> ActionItems;

	/** List View widget for representing our Actions List*/
	TSharedPtr<SListView<TSharedPtr<ActionType>>> ListView;

	/** Panel Style reference. */
	const FRCPanelStyle* RCPanelStyle;
};