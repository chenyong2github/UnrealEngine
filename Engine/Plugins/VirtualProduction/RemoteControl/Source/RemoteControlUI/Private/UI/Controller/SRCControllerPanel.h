// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UI/BaseLogicUI/SRCLogicPanelBase.h"

/*
* ~ SRCControllerPanel ~
*
* UI Widget for Controller Panel.
* Contains a header (Add/Remove/Empty) and List of Controllers
*/
class REMOTECONTROLUI_API SRCControllerPanel : public SRCLogicPanelBase
{
public:
	SLATE_BEGIN_ARGS(SRCControllerPanel)
		{
		}

	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs, const TSharedRef<SRemoteControlPanel>& InPanel);

private:
	/** Widget representing List of Controllers */
	TSharedPtr<class SRCControllerPanelList> ControllerPanelList;

public:
	/** Whether the Controller list widget currently has focus. Used for Delete Item UI command */
	bool IsListFocused() const;

	/** Delete Item UI command implementation for this panel */
	virtual void DeleteSelectedPanelItem() override;
};
