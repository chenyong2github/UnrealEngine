// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"

class SEmptyTab : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SEmptyTab) {}
	SLATE_END_ARGS()

	/**
	 * Constructs the widget.
	 */
	void Construct(const FArguments& InArgs);

private:

	FReply OnOpenProjectClicked();

};
