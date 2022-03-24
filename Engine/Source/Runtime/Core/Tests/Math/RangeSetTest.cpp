// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"
#include "Math/Range.h"
#include "Math/RangeSet.h"
#include "Misc/Timespan.h"
#include "TestHarness.h"

TEST_CASE("Core::Math::TRangeSet::Smoke Test", "[Core][Math][Smoke]")
{
	{
		TRangeSet<int32> RangeSet;
		RangeSet.Add(TRange<int32>::Inclusive(0, 1));
		RangeSet.Add(TRange<int32>::Inclusive(1, 2));
		RangeSet.Add(TRange<int32>::Inclusive(3, 4));

		int32 Value = RangeSet.GetMinBoundValue();
		CHECK(Value==0);
		
		Value = RangeSet.GetMaxBoundValue();
		CHECK(Value==4);
	}

	{
		TRangeSet<FTimespan> RangeSet;
		RangeSet.Add(TRange<FTimespan>::Inclusive(FTimespan(0), FTimespan(1)));
		RangeSet.Add(TRange<FTimespan>::Inclusive(FTimespan(1), FTimespan(2)));
		RangeSet.Add(TRange<FTimespan>::Inclusive(FTimespan(3), FTimespan(4)));

		FTimespan Value = RangeSet.GetMinBoundValue();
		CHECK(Value == FTimespan::Zero());
		
		Value = RangeSet.GetMaxBoundValue();
		CHECK(Value==FTimespan(4));
	}

}

