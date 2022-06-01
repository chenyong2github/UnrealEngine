// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Behaviour/RCBehaviourNode.h"
#include "UI/BaseLogicUI/SRCLogicPanelHeaderBase.h"

class FRCControllerModel;
class SRCBehaviourPanel;

/**
 * Behaviour panel UI Header widget.
 *
 * Contains buttons for Add / Remove / Empty
 */
class REMOTECONTROLUI_API SRCBehaviourPanelHeader : public SRCLogicPanelHeaderBase
{
public:
	SLATE_BEGIN_ARGS(SRCBehaviourPanelHeader)
		{
		}

	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs, TSharedRef<SRCBehaviourPanel> InBehaviourPanel, TSharedPtr<FRCControllerModel> InControllerItem);

private:
	/** Builds a menu containing the list of all possible Behaviours */
	TSharedRef<SWidget> GetBehaviourMenuContentWidget();

	/** Handles click event for Add Behaviour button*/
	void OnAddBehaviourClicked(UClass* InClass);

	/** Handles click event for "Empty" button; clears all Behaviours from the panel*/
	FReply OnClickEmptyButton();
	
private:
	/** The parent Behaviour Panel UI widget holding this header*/
	TWeakPtr<SRCBehaviourPanel> BehaviourPanelWeakPtr;
	
	/** The Controller (UI model) associated with our Behaviour panel*/
	TWeakPtr<FRCControllerModel> ControllerItemWeakPtr;
};
