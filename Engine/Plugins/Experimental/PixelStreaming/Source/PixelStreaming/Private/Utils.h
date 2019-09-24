// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include <chrono>

// uses chrono library to have comparable timestamps between UE4 and webrtc app
inline uint64 NowMs()
{
	//return static_cast<uint64>(FPlatformTime::Cycles64() * FPlatformTime::GetSecondsPerCycle64() * 1000);

	//double secs = FPlatformTime::Seconds();
	//// for the trick look at `FWindowsPlatformTime::Seconds()`
	//return static_cast<uint64>((secs - 16777216) * 1000);

	using namespace std::chrono;
	system_clock::duration now = system_clock::now().time_since_epoch();
	return duration_cast<milliseconds>(now - duration_cast<minutes>(now)).count();
}
