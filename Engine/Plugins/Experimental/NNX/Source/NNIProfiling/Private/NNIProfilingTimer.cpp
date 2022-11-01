// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNIProfilingTimer.h"
#include "HAL/PlatformTime.h"

namespace UE::NNIProfiling::Internal
{
	void FTimer::Tic()
	{
		TimeStart = { FPlatformTime::Seconds() };
	}

	double FTimer::Toc() const
	{
		return (FPlatformTime::Seconds() - TimeStart) * 1e3;
	}
} // UE::NNIProfiling::Internal