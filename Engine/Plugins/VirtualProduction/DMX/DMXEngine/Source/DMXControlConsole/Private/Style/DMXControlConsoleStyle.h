// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"


/**  DMX ControlConsole Editor Style */
class FDMXControlConsoleStyle
	: public FSlateStyleSet
{
public:
	/** Constructor */
	FDMXControlConsoleStyle();

	/** Desonstructor */
	virtual ~FDMXControlConsoleStyle();

	/**  Returns the style instance */
	static const FDMXControlConsoleStyle& Get();
};
