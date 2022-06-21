// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UI/BaseLogicUI/SRCLogicPanelListBase.h"

struct FRCPanelStyle;
class FRCBehaviourModel;
class FRCActionModel;
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
class REMOTECONTROLUI_API SRCActionPanelList : public SRCLogicPanelListBase
{
public:
	SLATE_BEGIN_ARGS(SRCActionPanelList)
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
	bool IsListFocused() const;

	/** Deletes currently selected items from the list view*/
	void DeleteSelectedPanelItem();

private:

	/** OnGenerateRow delegate for the Actions List View*/
	TSharedRef<ITableRow> OnGenerateWidgetForList( TSharedPtr<FRCActionModel> InItem, const TSharedRef<STableViewBase>& OwnerTable );

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

private:

	/** The parent Action Panel widget*/
	TWeakPtr<SRCActionPanel> ActionPanelWeakPtr;

	/** The Behaviour (UI model) associated with us*/
	TWeakPtr<FRCBehaviourModel> BehaviourItemWeakPtr;

	/** List of Actions (UI model) active in this widget */
	TArray<TSharedPtr<FRCActionModel>> ActionItems;

	/** List View widget for representing our Actions List*/
	TSharedPtr<SListView<TSharedPtr<FRCActionModel>>> ListView;

	/** Panel Style reference. */
	const FRCPanelStyle* RCPanelStyle;
};

