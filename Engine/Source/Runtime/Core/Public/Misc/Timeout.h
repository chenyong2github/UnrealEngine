// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/Timespan.h"
#include "HAL/PlatformTime.h"
#include "CoreTypes.h"

namespace UE
{
	// utility class to handle timeouts
	// usage:
	// ------------------------------------
	// FTimeout Timeout(FTimespan::FromMilliseconds(2));
	// while (!Timeout) { ... }
	// ------------------------------------
	class FTimeout
	{
	public:
		explicit FTimeout(FTimespan Value)
			: Timeout(Value)
		{
		}

		explicit operator bool() const
		{
			return GetRemainingTime() <= FTimespan::Zero();
		}

		FTimespan GetElapsedTime() const
		{
			return FTimespan::FromSeconds(FPlatformTime::Seconds()) - Start;
		}

		FTimespan GetRemainingTime() const
		{
			return Timeout == FTimespan::MaxValue() ? FTimespan::MaxValue() : Timeout - GetElapsedTime();
		}

		static FTimeout Never()
		{
			return FTimeout{ FTimespan::MaxValue() };
		}

		FTimespan GetTimeoutValue() const
		{
			return Timeout;
		}

		friend bool operator==(FTimeout Left, FTimeout Right)
		{
			return Left.Timeout == Right.Timeout && (Left.Timeout == FTimespan::MaxValue() || Left.Start == Right.Start);
		}

	private:
		FTimespan Start{ FTimespan::FromSeconds(FPlatformTime::Seconds()) };
		FTimespan Timeout;
	};
}
