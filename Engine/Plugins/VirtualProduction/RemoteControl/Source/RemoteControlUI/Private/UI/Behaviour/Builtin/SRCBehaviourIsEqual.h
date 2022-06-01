// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "RemoteControlField.h"

class FRCIsEqualBehaviourModel;

/*
* ~ SRCBehaviourIsEqual ~
*
* Behaviour specific details panel for Is Equal Behaviour
* Contains a Value panel containing an input field of the active Controller's type and related functionality
*/
class REMOTECONTROLUI_API SRCBehaviourIsEqual : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRCBehaviourIsEqual)
		{
		}

	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs, TSharedRef<const FRCIsEqualBehaviourModel> InBehaviourItem);

private:
	/** The Behaviour (UI model) associated with us*/
	TWeakPtr<const FRCIsEqualBehaviourModel> IsEqualBehaviourItemWeakPtr;
};
