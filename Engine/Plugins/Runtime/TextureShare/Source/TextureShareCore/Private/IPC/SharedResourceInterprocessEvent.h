// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "HAL/Event.h"
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"

class FTextureShareEventWin
	: public FEvent
{
public:
	FTextureShareEventWin(const FString& InEventName);
	virtual ~FTextureShareEventWin();

	// FEvent
	virtual bool Create(bool bIsManualReset = false) override;
	virtual void Trigger() override;
	virtual void Reset() override;
	virtual bool Wait(uint32 WaitTime, const bool bIgnoreThreadIdleStats = false) override;
	virtual bool IsManualReset() override
	{
		return true;
	}
	//~ FEvent

private:
	bool IsEnabled() const
	{
		return Event != nullptr;
	}

private:
	/** Holds the handle to the event. */
	HANDLE Event;

	/** Holds the global name of the event. */
	FString EventName;
};

#include "Windows/HideWindowsPlatformTypes.h"
