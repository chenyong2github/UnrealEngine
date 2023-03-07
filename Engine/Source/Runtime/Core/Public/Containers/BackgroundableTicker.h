// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Delegates/IDelegateInstance.h"
#include "Ticker.h"

/**
 * This works the same as the core FTSTicker, but on supported mobile platforms
 * it continues ticking while the app is running in the background.
 */
class CORE_API FTSBackgroundableTicker
	: public FTSTicker
{
public:
	static FTSBackgroundableTicker& GetCoreTicker();

	FTSBackgroundableTicker();
	~FTSBackgroundableTicker();

private:
	FDelegateHandle CoreTickerHandle;
	::FDelegateHandle BackgroundTickerHandle;
	bool bWasBackgrounded = false;
};
