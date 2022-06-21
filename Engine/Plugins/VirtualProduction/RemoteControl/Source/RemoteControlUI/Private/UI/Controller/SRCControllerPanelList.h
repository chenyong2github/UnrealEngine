// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "UI/BaseLogicUI/SRCLogicPanelListBase.h"

struct FRCPanelStyle;
class FRCControllerModel;
class FRCLogicModeBase;
class IPropertyRowGenerator;
class ITableRow;
class ITableBase;
class SRCControllerPanel;
class STableViewBase;
class URCController;
class URemoteControlPreset;
template <typename ItemType> class SListView;

/*
* ~ SRCControllerPanelList ~
*
* UI Widget for Controllers List
* Used as part of the RC Logic Actions Panel.
*/
class REMOTECONTROLUI_API SRCControllerPanelList : public SRCLogicPanelListBase
{
public:
	SLATE_BEGIN_ARGS(SRCControllerPanelList)
		{
		}

	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs, const TSharedRef<SRCControllerPanel> InControllerPanel);

	/** Returns true if the underlying list is valid and empty. */
	virtual bool IsEmpty() const override;

	/** Returns number of items in the list. */
	virtual int32 Num() const override;

	/** Whether the Controllers List View currently has focus.*/
	bool IsListFocused() const;

	/** Deletes currently selected items from the list view*/
	void DeleteSelectedPanelItem();

	void EnterRenameMode();

private:

	/** OnGenerateRow delegate for the Actions List View */
	TSharedRef<ITableRow> OnGenerateWidgetForList( TSharedPtr<FRCControllerModel> InItem, const TSharedRef<STableViewBase>& OwnerTable );
	
	/** OnSelectionChanged delegate for Actions List View */
	void OnTreeSelectionChanged(TSharedPtr<FRCControllerModel> InItem , ESelectInfo::Type);

	/** Responds to the selection of a newly created Controller. Resets UI state */
	void OnControllerAdded(const FName& InNewPropertyName);
	
	/** Responds to the removal of all Controllers. Resets UI state */
	void OnEmptyControllers();

	/** Change listener for Controllers. Bound to the PropertyRowGenerator's delegate
	* This is propagated to the corresponding Controller model (Virtual Property) for evaluating all associated Behaviours.
	*/
	void OnFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent);

	/** The row generator used to represent each Controller as a row, when used with SListView */
	TSharedPtr<IPropertyRowGenerator> PropertyRowGenerator;

	/** The currently selected Controller item (UI model) */
	TWeakPtr<FRCControllerModel> SelectedControllerItemWeakPtr = nullptr;
	
	/** The parent Controller Panel widget */
	TWeakPtr<SRCControllerPanel> ControllerPanelWeakPtr;

	/** List of Controllers (UI model) active in this widget */
	TArray<TSharedPtr<FRCControllerModel>> ControllerItems;

	/** List View widget for representing our Controllers List*/
	TSharedPtr<SListView<TSharedPtr<FRCControllerModel>>>  ListView;
	
	/** Refreshes the list from the latest state of the model*/
	virtual void Reset() override;

	/** Handles broadcasting of a successful remove item operation.*/
	virtual void BroadcastOnItemRemoved() override;

	/** Fetches the Remote Control preset associated with the parent panel */
	virtual URemoteControlPreset* GetPreset() override;

	/** Removes the given Controller UI model item from the list of UI models*/
	virtual int32 RemoveModel(const TSharedPtr<FRCLogicModeBase> InModel) override;

	/** Panel Style reference. */
	const FRCPanelStyle* RCPanelStyle;
};

