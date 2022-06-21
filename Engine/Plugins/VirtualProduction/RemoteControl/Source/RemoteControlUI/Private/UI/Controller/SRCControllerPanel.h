// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PropertyBag.h"
#include "UI/BaseLogicUI/SRCLogicPanelBase.h"

enum class EPropertyBagPropertyType : uint8;
struct FRCPanelStyle;
class SRCControllerPanel;

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

	/** Whether the Controller list widget currently has focus. Used for Delete Item UI command */
	bool IsListFocused() const;

	/** Delete Item UI command implementation for this panel */
	virtual void DeleteSelectedPanelItem() override;

	void EnterRenameMode();

protected:

	/** Warns user before deleting all items in a panel. */
	virtual FReply RequestDeleteAllItems() override;

private:

	/** Builds a menu containing the list of all possible Controllers*/
	TSharedRef<SWidget> GetControllerMenuContentWidget() const;

	/** Handles click event for Add Controller button*/
	void OnAddControllerClicked(const EPropertyBagPropertyType InValueType, UObject* InValueTypeObject = nullptr) const;

	/** Handles click event for Empty button; clears all controllers from the panel*/
	FReply OnClickEmptyButton();

private:

	/** Widget representing List of Controllers */
	TSharedPtr<class SRCControllerPanelList> ControllerPanelList;

	/** Panel Style reference. */
	const FRCPanelStyle* RCPanelStyle;
};
