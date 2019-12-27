// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Ticker.h"

class FBackgroundableTicker
	: public FTicker
{
public:

	CORE_API static FBackgroundableTicker& GetCoreTicker();

	CORE_API FBackgroundableTicker();
	CORE_API ~FBackgroundableTicker();

private:
	
	FDelegateHandle CoreTickerHandle;
	FDelegateHandle BackgroundTickerHandle;
	bool bWasBackgrounded = false;
};