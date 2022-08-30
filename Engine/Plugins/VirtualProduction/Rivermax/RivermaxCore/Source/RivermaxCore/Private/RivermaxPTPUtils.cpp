// Copyright Epic Games, Inc. All Rights Reserved.

#include "RivermaxPTPUtils.h"

#include "Misc/FrameRate.h"
#include "RivermaxTypes.h"

namespace UE::RivermaxCore
{
	uint64 GetNextAlignmentPoint(const uint64 InPTPTimeNanosec, const FFrameRate& InRate)
	{
		// Based on ST2059. Formulae from PTP time to next alignment point
		// NextAlignmentPoint = Floor( (TimeNs / IntervalNs + 1) * IntervalNs)
		// Time involved don't play well with double. Need to keep fixed point as much as possible

		// First part of formulae is to get the associated frame number of current time
		const uint64 CurrentFrameNumber = GetFrameNumber(InPTPTimeNanosec, InRate);

		// We want to get the time of the following frame number
		return GetAlignmentPointFromFrameNumber(CurrentFrameNumber + 1, InRate);
	}

	uint64 GetAlignmentPointFromFrameNumber(const uint64 InFrameNumber, const FFrameRate& InRate)
	{
		// Based on ST2059. Formulae from PTP time to next alignment point
		// NextAlignmentPoint = Floor( (CurrentFrameNumber + 1) * IntervalNs)

		// Example for frame F=40113798005 at 23.97
		// uint64 T = F / 24000 = 1671408
		// uint64 Tr =F % 24000 = 6005
		// uint64 TT = T * 1001 = 1673079408
		// uint64 TTr = 6005 * 1001 = 6011005
		// uint64 TTrr = TTr * Scale / 24000 = 250458541666
		// return TT * Scale + TTrr = 1673079658458541666

		const uint64 Nanoscale = 1E9;

		const uint64 Intermediate = InFrameNumber / InRate.Numerator;
		const uint64 IntermediateRemainder = InFrameNumber % InRate.Numerator;
		const uint64 NextAlignment = Intermediate * InRate.Denominator;
		const uint64 NextAlignementRemainderInterm = (IntermediateRemainder * InRate.Denominator);
		const uint64 NextAlignmentRemainder = (NextAlignementRemainderInterm * Nanoscale) / InRate.Numerator;

		return (NextAlignment * Nanoscale) + NextAlignmentRemainder;
	}

	uint64 GetFrameNumber(const uint64 InPTPTimeNanosec, const FFrameRate& InRate)
	{
		const uint64 NanoScale = 1E9;

		// Util struct returned for intermediate calculation of frame number
		struct FResult
		{
			uint64 FrameCount = 0;
			double Fraction = 0.0;
		};

		const auto CalculateFrameNumberFunc = [InRate](const uint64 PTPTime, uint64 Scale) -> FResult
		{
			// Example for PTP time 1673079658458541667 at 23.97
			
			// Seconds part (1673079658). Scale = 1. Remainder = 0.
			// uint64 F = Time / 1001 = 1671408
			// uint64 Fr = Time % 1001 = 250
			// uint64 FF = F * 24000 = 40113792000
			// uint64 FFr = (Fr * 24000) / 1001 = 5994
			// uint64 FFrr =(Fr * 24000) % 1001) = 6
			// double Tr = (FFrr / (double)1001.0 = 0.005994005994
			// uint64 Frame = (FF + FFr) / Scale = 40113797994
			// double FractionalFrame = Frac((FF + FFr) / Scale) + Tr / Scale = 0.005994005994
			// ret {Frame, Tr} = {40113797994, 0.005994005994}

			// Nanoseconds part (458541667). Scale = 1E9. Remainder = 0.005994005994 * 1E9 = 5994005
			// uint64 F = Time / 1001 = 458083
			// uint64 Fr = Time % 1001 = 584
			// uint64 FF = F * 24000 = 10993992000
			// uint64 FFr = (Fr * 24000) / 1001 = 14001
			// uint64 FFrr =(Fr * 24000) % 1001) = 999
			// double Tr = (FFrr / (double)1001.0 = 0.998001998001998002
			// uint64 Frame = (FF + FFr) / Scale = 10
			// ret {Frame, Tr} = {10, .994006001998002}

			// Full frames = 40113797994 + 10 =  40113798004
			// Fractional frames = 1.0000000079920019 ( 0.005994005994 + 0.994006001998002 )
			// We round to the 1/1000000th of a frame to cover cases where fractional frame is 0.999999
			// Final result= 40113798004 + 1 = 40113798005


			const uint64 FrameIntermediate = PTPTime / InRate.Denominator;
			const uint64 FrameRemainderIntermediate = PTPTime % InRate.Denominator;
			const uint64 FrameInteger = FrameIntermediate * InRate.Numerator;
			const uint64 FrameIntegerRemainder = (FrameRemainderIntermediate * InRate.Numerator) / InRate.Denominator;
			const double TrailingRemainder = ((FrameRemainderIntermediate * InRate.Numerator) % InRate.Denominator) / (double)InRate.Denominator;
			const uint64 FrameNumber = (FrameInteger + FrameIntegerRemainder) / Scale;

			const double FractionalFrame  = FMath::Frac((FrameInteger + FrameIntegerRemainder) / (double)Scale) + TrailingRemainder / Scale;
			return FResult{ FrameNumber, FractionalFrame };
		};

		// Split incoming time in nanoseconds in two parts : seconds and nanoseconds
		// Even after dividing by frame rate denominator (i.e 1001), we might overflow uint64 when multiplying by frame rate numerator (i.e. 24000)
		const uint64 PTPSeconds = InPTPTimeNanosec / NanoScale;
		const uint64 PTPNanoSeconds = InPTPTimeNanosec % NanoScale;
		
		// Proceed with seconds part. No scaling
		const FResult FrameNumber1 = CalculateFrameNumberFunc(PTPSeconds, 1);

		// Proceed with nanoseconds using a scale of 1E9 to get actual frame numbers
		const FResult FrameNumber2 = CalculateFrameNumberFunc(PTPNanoSeconds, NanoScale);

		// Add up full frame number
		const uint64 FrameNumber = FrameNumber1.FrameCount + FrameNumber2.FrameCount;

		// Add up fractional frame number from intermediate calculation
		// and round to the 1/100000th of a frame to avoid returning the previous time when the incoming time
		// is almost exactly on alignment
		const double FramePrecision = 100000; 
		const double FractionFrameNumber = FrameNumber1.Fraction + FrameNumber2.Fraction;
		const uint64 TrailingFrameNumber = FMath::RoundToDouble(FractionFrameNumber * FramePrecision) / FramePrecision;
		return FrameNumber + TrailingFrameNumber;
	}
}