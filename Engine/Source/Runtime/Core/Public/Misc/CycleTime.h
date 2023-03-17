// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Math/NumericLimits.h"

#define UE_API CORE_API

struct FTimespan;

namespace UE
{

/**
 * A span of time between two values of FPlatformTime::Cycles64().
 *
 * @see FCycleTimePoint
 */
struct FCycleTimeSpan
{
public:
	UE_API explicit FCycleTimeSpan(FTimespan Span);

	constexpr static FCycleTimeSpan Zero() { return FCycleTimeSpan(); }

	constexpr static FCycleTimeSpan Infinity() { return FromCycles(MAX_int64); }

	constexpr static FCycleTimeSpan FromCycles(const int64 InCycles)
	{
		FCycleTimeSpan TimeSpan;
		TimeSpan.Cycles = InCycles;
		return TimeSpan;
	}

	constexpr FCycleTimeSpan() = default;

	constexpr int64 GetCycles() const { return Cycles; }

	constexpr bool IsInfinity() const { return (Cycles == MAX_int64) | (Cycles == MIN_int64); }

	constexpr bool operator==(const FCycleTimeSpan Other) const { return Cycles == Other.Cycles; }
	constexpr bool operator!=(const FCycleTimeSpan Other) const { return Cycles != Other.Cycles; }
	constexpr bool operator<=(const FCycleTimeSpan Other) const { return Cycles <= Other.Cycles; }
	constexpr bool operator< (const FCycleTimeSpan Other) const { return Cycles <  Other.Cycles; }
	constexpr bool operator>=(const FCycleTimeSpan Other) const { return Cycles >= Other.Cycles; }
	constexpr bool operator> (const FCycleTimeSpan Other) const { return Cycles >  Other.Cycles; }

	constexpr FCycleTimeSpan operator+() const { return *this; }
	constexpr FCycleTimeSpan operator-() const
	{
		return UNLIKELY(Cycles == MIN_int64) ? FromCycles(MAX_int64)
			 : UNLIKELY(Cycles == MAX_int64) ? FromCycles(MIN_int64)
											 : FromCycles(-Cycles);
	}

	constexpr FCycleTimeSpan operator+(const FCycleTimeSpan Span) const
	{
		return UNLIKELY(IsInfinity()) ? *this : UNLIKELY(Span.IsInfinity()) ? Span : FromCycles(Cycles + Span.GetCycles());
	}

	constexpr FCycleTimeSpan operator-(const FCycleTimeSpan Span) const
	{
		return UNLIKELY(IsInfinity()) ? *this : UNLIKELY(Span.IsInfinity()) ? -Span : FromCycles(Cycles - Span.GetCycles());
	}

	UE_API double ToSeconds() const;
	UE_API double ToMilliseconds() const;

	UE_API static FCycleTimeSpan FromSeconds(double Seconds);
	UE_API static FCycleTimeSpan FromMilliseconds(double Milliseconds);

private:
	int64 Cycles = 0;
};

/**
 * A point in time as reported by FPlatformTime::Cycles64().
 *
 * This is a monotonic clock which means the current time will never decrease. This time is meant
 * primarily for measuring intervals. The interval between ticks of this clock is constant except
 * for the time that the system is suspended on certain platforms. The tick frequency will differ
 * between platforms, and must not be used as a means of communicating time without communicating
 * the tick frequency together with the time.
 */
struct FCycleTimePoint
{
public:
	UE_API static FCycleTimePoint Now();

	constexpr static FCycleTimePoint Infinity() { return FromCycles(MAX_uint64); }

	constexpr static FCycleTimePoint FromCycles(const uint64 InCycles)
	{
		FCycleTimePoint TimePoint;
		TimePoint.Cycles = InCycles;
		return TimePoint;
	}

	constexpr FCycleTimePoint() = default;

	constexpr int64 GetCycles() const { return Cycles; }

	constexpr bool IsInfinity() const { return Cycles == MAX_uint64; }

	constexpr bool operator==(const FCycleTimePoint Other) const { return Cycles == Other.Cycles; }
	constexpr bool operator!=(const FCycleTimePoint Other) const { return Cycles != Other.Cycles; }
	constexpr bool operator<=(const FCycleTimePoint Other) const { return Cycles <= Other.Cycles; }
	constexpr bool operator< (const FCycleTimePoint Other) const { return Cycles <  Other.Cycles; }
	constexpr bool operator>=(const FCycleTimePoint Other) const { return Cycles >= Other.Cycles; }
	constexpr bool operator> (const FCycleTimePoint Other) const { return Cycles >  Other.Cycles; }

	constexpr FCycleTimePoint operator+(const FCycleTimeSpan Span) const
	{
		return UNLIKELY(IsInfinity() || Span.IsInfinity()) ? Infinity() : FromCycles(Cycles + Span.GetCycles());
	}

	constexpr FCycleTimePoint operator-(const FCycleTimeSpan Span) const
	{
		return UNLIKELY(IsInfinity() || Span.IsInfinity()) ? Infinity() : FromCycles(Cycles - Span.GetCycles());
	}

	constexpr FCycleTimeSpan operator-(const FCycleTimePoint Point) const
	{
		return UNLIKELY(IsInfinity()) ? FCycleTimeSpan::Infinity()
			 : UNLIKELY(Point.IsInfinity()) ? -FCycleTimeSpan::Infinity()
			 : FCycleTimeSpan::FromCycles(int64(Cycles - Point.Cycles));
	}

private:
	uint64 Cycles = 0;
};

} // namespace UE

#undef UE_API
