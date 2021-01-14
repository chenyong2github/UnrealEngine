// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Layout/SBorder.h"

enum class EMessageStyle
{
	Warning,
	Error
};

class EDITORWIDGETS_API SWarningOrErrorBox : public SBorder
{
public:
	SLATE_BEGIN_ARGS(SWarningOrErrorBox)
		: _MessageStyle(EMessageStyle::Warning)
	{}
		SLATE_ATTRIBUTE(FText, Message)
		SLATE_ARGUMENT(EMessageStyle, MessageStyle)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};