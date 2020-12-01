// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

namespace Trace {
namespace TraceServices {

////////////////////////////////////////////////////////////////////////////////
class FTrackerTime
{
public:
					FTrackerTime(uint32 Period) {}
	void			AddSample(uint64 Cycles) {}
	double			GetTimeBySerial(uint32 Serial, bool bInterpolate=false) const;
	uint64			GetCykelBySerial(uint32 Serial, bool bInterpolate=false) const;
	uint32			GetSerialByCycle(uint64 Cycle, bool bInterpolate=false) const;

private:
	TArray<uint64>	Samples;
	uint32			Period;
};

} // namespace TraceServices
} // namespace Trace

/* vim: set noet : */
