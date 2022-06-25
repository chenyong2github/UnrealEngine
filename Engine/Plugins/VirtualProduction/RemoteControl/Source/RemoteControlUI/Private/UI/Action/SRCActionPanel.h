// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UI/BaseLogicUI/SRCLogicPanelBase.h"

struct FRCPanelStyle;
struct FRemoteControlField;
class FRCBehaviourModel;
class SBox;
class SRemoteControlPanel;
/*
* ~ SRCActionPanel ~
*
* UI Widget for Action Panel.
* Contains a header, footer, list of actions and a behaviour specific panel
*/
class REMOTECONTROLUI_API SRCActionPanel : public SRCLogicPanelBase
{
public:
	SLATE_BEGIN_ARGS(SRCActionPanel)
		{
		}

	SLATE_END_ARGS()
	
	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs, const TSharedRef<SRemoteControlPanel>& InPanel);

	/** Shutdown panel */
	static void Shutdown();

	/** Whether the Actions list widget currently has focus.*/
	bool IsListFocused() const;

	/** Delete Item UI command implementation for this panel */
	virtual void DeleteSelectedPanelItem() override;

protected:

	/** Warns user before deleting all items in a panel. */
	virtual FReply RequestDeleteAllItems() override;

private:

	/** Determines the visibility Add All Button. */
	EVisibility HandleAddAllButtonVisibility() const;

	/** 
	* Behaviour selection change listener.
	* Updates the list of actions from newly selected Behaviour
	*/
	void OnBehaviourSelectionChanged(TSharedPtr<FRCBehaviourModel> InBehaviourItem);

	/* Rebuilds the Action Panel for a newly selected Behaviour*/
	void UpdateWrappedWidget(TSharedPtr<FRCBehaviourModel> InBehaviourItem = nullptr);

	/** Handles click event for Open Behaviour Blueprint button*/
	FReply OnClickOverrideBlueprintButton();

	/**
	 * Builds a menu containing the list of all possible Actions
	 * These are derived from the list of Exposed entities of the Remote Control Preset associated with us.
	 */
	TSharedRef<SWidget> GetActionMenuContentWidget();

	/** Handles click event for Add Action button*/
	void OnAddActionClicked(TSharedPtr<FRemoteControlField> InRemoteControlField);

	/** Handles click event for Empty button; clear all Actions from the panel*/
	FReply OnClickEmptyButton();

	/** Handles click event for Add All button; Adds all possible actions for the active Remote Control Preset*/
	FReply OnAddAllFields();

private:

	/** The parent Behaviour that this Action panel is associated with */
	TWeakPtr<FRCBehaviourModel> SelectedBehaviourItemWeakPtr;
	
	/** Parent Box for the entire widget */
	TSharedPtr<SBox> WrappedBoxWidget;

	/** Widget representing List of Actions */
	TSharedPtr<class SRCActionPanelList> ActionPanelList;

	/** Helper widget for behavior details. */
	static TSharedPtr<SBox> NoneSelectedWidget;

	/** Panel Style reference. */
	const FRCPanelStyle* RCPanelStyle;
};
