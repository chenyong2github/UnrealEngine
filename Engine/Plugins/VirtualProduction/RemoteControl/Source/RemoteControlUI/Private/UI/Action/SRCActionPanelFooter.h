// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "RemoteControlField.h"
#include "UI/BaseLogicUI/SRCLogicPanelHeaderBase.h"

class SRCActionPanel;
class FRCBehaviourModel;

/**
 *  Action panel UI Footer widget.
 * Contains Add/ Add All / Remove/Empty buttons for managing the Actions list
 */
class REMOTECONTROLUI_API SRCActionPanelFooter : public SRCLogicPanelHeaderBase
{
public:
	SLATE_BEGIN_ARGS(SRCActionPanelFooter)
		{
		}

	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs, TSharedRef<SRCActionPanel> InActionPanel, TSharedRef<FRCBehaviourModel> InBehaviourItem);

private:
	/** Builds a menu containing the list of all possible Actions
	* These are derived from the list of Exposed entities of the Remote Control Preset associated with us */
	TSharedRef<SWidget> GetActionMenuContentWidget();

	/** Handles click event for Add Action button*/
	void OnAddActionClicked(TSharedPtr<FRemoteControlField> InRemoteControlField);

	/** Handles click event for Empty button; clear all Actions from the panel*/
	FReply OnClickEmptyButton();
	
	/** Handles click event for Add All button; Adds all possible actions for the active Remote Control Preset*/
	FReply OnAddAllField();

	/** The parent Action panel holding this Footer widget*/
	TWeakPtr<SRCActionPanel> ActionPanelWeakPtr;
	
	/** The active Behaviour (UI Model) associated with the Actions being shown*/
	TWeakPtr<FRCBehaviourModel> BehaviourItemWeakPtr;
};
