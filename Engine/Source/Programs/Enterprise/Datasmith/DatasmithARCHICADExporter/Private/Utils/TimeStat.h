// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AddonTools.h"

BEGIN_NAMESPACE_UE_AC

// Get cpu and real time
class FTimeStat
{
  public:
	// Contructor (Get current process CPU time and real time)
	FTimeStat() { ReStart(); }

	// Reset to current process CPU time and real time
	void ReStart();

	// Print time differences
	void PrintDiff(const char* InStatLabel, const FTimeStat& InStart);

	// Tool get current real time clock
	static double RealTimeClock();

	// Tool get process CPU real time clock
	static double CpuTimeClock();

  private:
	// Time at the creation of this object
	double CpuTime;
	double RealTime;
};

END_NAMESPACE_UE_AC
