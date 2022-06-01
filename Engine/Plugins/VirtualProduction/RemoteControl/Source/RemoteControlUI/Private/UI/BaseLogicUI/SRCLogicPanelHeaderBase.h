// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

/**
 *  Base UI widget for Logic header panels.
 * Typically contain metdata, Add/Remove buttons, etc; 
 * Currently customized across Controllers / Behaviours / Actions
 */
class REMOTECONTROLUI_API SRCLogicPanelHeaderBase : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRCLogicPanelHeaderBase)
		{
		}

	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs);
};
