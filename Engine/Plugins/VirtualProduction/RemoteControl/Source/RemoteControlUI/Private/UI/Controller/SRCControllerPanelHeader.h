// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PropertyBag.h"
#include "UI/BaseLogicUI/SRCLogicPanelHeaderBase.h"

enum class EPropertyBagPropertyType : uint8;
class SRCControllerPanel;

/**
 * Controller panel UI Header widget.
 *
 * Contains buttons for Add / Remove / Empty
 */
class REMOTECONTROLUI_API SRCControllerPanelHeader : public SRCLogicPanelHeaderBase
{
public:
	SLATE_BEGIN_ARGS(SRCControllerPanelHeader)
		{
		}

	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs, const TSharedRef<SRCControllerPanel> InControllerPanel);

private:
	/** Builds a menu containing the list of all possible Controllers*/
	TSharedRef<SWidget> GetControllerMenuContentWidget() const;

	/** Handles click event for Add Controller button*/
	void OnAddControllerClicked(const EPropertyBagPropertyType InValueType, UObject* InValueTypeObject = nullptr) const;

	/** Handles click event for Empty button; clears all controllers from the panel*/
	FReply OnClickEmptyButton();

	/** The parent Controller panel UI widget holding this header */
	TWeakPtr<SRCControllerPanel> ControllerPanelWeakPtr;
};

