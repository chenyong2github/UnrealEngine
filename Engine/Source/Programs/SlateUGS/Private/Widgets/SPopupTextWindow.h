// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SWindow.h"

class SPopupTextWindow : public SWindow
{
public:
	SLATE_BEGIN_ARGS(SPopupTextWindow)
		: _BodyTextJustification(ETextJustify::Center)
		{}
		SLATE_ARGUMENT(FText, TitleText)
		SLATE_ARGUMENT(FText, BodyText)
		SLATE_ARGUMENT(ETextJustify::Type, BodyTextJustification)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};
