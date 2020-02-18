// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"

/**
 * Slate style set that defines all the styles for the monitor UI
 */
class TIMEDDATAMONITOR_API FTimedDataMonitorStyle : public FSlateStyleSet
{
public:
	/** Access the singleton instance for this style set */
	static FTimedDataMonitorStyle& Get();

private:
	FTimedDataMonitorStyle();
	~FTimedDataMonitorStyle();
};
