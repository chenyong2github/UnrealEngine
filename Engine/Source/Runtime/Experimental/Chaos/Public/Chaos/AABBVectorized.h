// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Core.h"
#include "ChaosArchive.h"
#include "Math/VectorRegister.h"
#include "Math/UnrealMathVectorConstants.h"
#include "Chaos/VectorUtility.h"


namespace Chaos
{

class FAABBVectorized
{
public:

	FORCEINLINE FAABBVectorized()
	{
		Min = GlobalVectorConstants::BigNumber;
		Max = VectorNegate(GlobalVectorConstants::BigNumber);
	}

	FORCEINLINE FAABBVectorized(const VectorRegister4Float& Min, const VectorRegister4Float& Max)
		: Min(Min)
		, Max(Max)
	{
	}

	FORCEINLINE const VectorRegister4Float& GetMin() const { return Min; }
	FORCEINLINE const VectorRegister4Float& GetMax() const { return Max; }

	FORCEINLINE_DEBUGGABLE bool RaycastFast(const VectorRegister4Float& StartPoint, const VectorRegister4Float& Dir, const VectorRegister4Float& InvDir, const bool* bParallel, const VectorRegister4Float& Length, const VectorRegister4Float& InvLength, VectorRegister4Float& OutEntryTime, VectorRegister4Float& OutExitTime) const
	{
		const VectorRegister4Float StartToMin = VectorSubtract(Min, StartPoint);
		const VectorRegister4Float StartToMax = VectorSubtract(Max, StartPoint);
		const VectorRegister4Float Parallel = MakeVectorRegisterFloat(~uint32(0) * static_cast<uint32>(bParallel[0]), ~uint32(0) * static_cast<uint32>(bParallel[1]), ~uint32(0) * static_cast<uint32>(bParallel[2]), 0);
		const VectorRegister4Float StarToMinGTZero = VectorCompareGT(StartToMin, VectorZero());
		const VectorRegister4Float ZeroGTStarToMax = VectorCompareGT(VectorZero(), StartToMax);
		VectorRegister4Float IsFalse = VectorBitwiseAnd(VectorBitwiseOr(StarToMinGTZero, ZeroGTStarToMax), Parallel);

		if (VectorMaskBits(IsFalse))
		{
			return false;	//parallel and outside
		}

		const VectorRegister4Float StartToMinInvDir = VectorMultiply(StartToMin, InvDir);
		const VectorRegister4Float StartToMaxInvDir = VectorMultiply(StartToMax, InvDir);

		VectorRegister4Float Time1 = VectorSelect(Parallel, VectorZero(), StartToMinInvDir);
		VectorRegister4Float Time2 = VectorSelect(Parallel, Length, StartToMaxInvDir);
		const VectorRegister4Float Time1GTTime2 = VectorCompareGT(Time1, Time2);

		// Maybe could be optimized with a xor
		const VectorRegister4Float TmpTime1 = VectorSelect(Time1GTTime2, Time2, Time1);
		Time2 = VectorSelect(Time1GTTime2, Time1, Time2);
		Time1 = TmpTime1;

		VectorRegister4Float LatestStartTime = VectorMax(Time1, VectorSwizzle(Time1, 1, 2, 0, 3));
		LatestStartTime = VectorMax(LatestStartTime, VectorSwizzle(Time1, 2, 0, 1, 3));
		LatestStartTime = VectorMax(LatestStartTime, VectorZero());

		VectorRegister4Float EarliestEndTime = VectorMin(Time2, VectorSwizzle(Time2, 1, 2, 0, 3));
		EarliestEndTime = VectorMin(EarliestEndTime, VectorSwizzle(Time2, 2, 0, 1, 3));
		EarliestEndTime = VectorMin(EarliestEndTime, Length);

		//Outside of slab before entering another
		IsFalse = VectorCompareGT(LatestStartTime, EarliestEndTime);

		if (VectorMaskBits(IsFalse))
		{
			return false; 
		}
		OutEntryTime = LatestStartTime;
		OutExitTime = EarliestEndTime;
		return true;
	}

private:
	VectorRegister4Float Min, Max;
};
}