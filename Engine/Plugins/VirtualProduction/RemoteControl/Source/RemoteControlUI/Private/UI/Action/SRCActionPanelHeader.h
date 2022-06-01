// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "RemoteControlField.h"
#include "UI/BaseLogicUI/SRCLogicPanelHeaderBase.h"

class SRCActionPanel;
class FRCBehaviourModel;

/**
 *  Action panel UI Header widget.
 * Contains metdata, Add/Remove buttons, etc;
 */
class REMOTECONTROLUI_API SRCActionPanelHeader : public SRCLogicPanelHeaderBase
{
public:
	SLATE_BEGIN_ARGS(SRCActionPanelHeader)
		{
		}

	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs, TSharedRef<SRCActionPanel> InActionPanel, TSharedRef<FRCBehaviourModel> InBehaviourItem);

private:

	/** Handles click event for Open Behaviour Blueprint button*/
	FReply OnClickOverrideBlueprintButton();

private:
	/** The parent Action panel UI widget holding this Header*/
	TWeakPtr<SRCActionPanel> ActionPanelWeakPtr;
	
	/** The active Behaviour (UI Model) associated with the Actions being shown*/
	TWeakPtr<FRCBehaviourModel> BehaviourItemWeakPtr;
};
