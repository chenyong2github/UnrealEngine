// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SWindow.h"

class SErrorWindow : public SWindow
{
public:
	SLATE_BEGIN_ARGS(SErrorWindow) {}
		SLATE_ARGUMENT(FText, ErrorText)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};
