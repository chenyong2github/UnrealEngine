// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "UI/BaseLogicUI/SRCLogicPanelBase.h"
#include "Widgets/Layout/SBox.h"

class FRCBehaviourModel;
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

private:
	/** 
	* Behaviour selection change listener.
	* Updates the list of actions from newly selected Behaviour
	*/
	void OnBehaviourSelectionChanged(TSharedPtr<FRCBehaviourModel> InBehaviourItem);

	/* Rebuilds the Action Panel for a newly selected Behaviour*/
	void UpdateWrappedWidget(TSharedPtr<FRCBehaviourModel> InBehaviourItem = nullptr);

	/** The parent Behaviour that this Action panel is associated with */
	TWeakPtr<FRCBehaviourModel> SelectedBehaviourItemWeakPtr;
	
	/** Parent Box for the entire widget */
	TSharedPtr<SBox> WrappedBoxWidget;

	/** Widget representing List of Actions */
	TSharedPtr<class SRCActionPanelList> ActionPanelList;

public:
	/** Whether the Actions list widget currently has focus.*/
	bool IsListFocused() const;

	/** Delete Item UI command implementation for this panel */
	virtual void DeleteSelectedPanelItem() override;
};
