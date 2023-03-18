// Copyright Epic Games, Inc. All Rights Reserved.

#pragma autortfm

#include <catch_amalgamated.hpp>
#include <AutoRTFM/AutoRTFM.h>

TEST_CASE("DeferUntilCommit")
{
    bool bDidRun = false;
    REQUIRE(
        AutoRTFM::ETransactionResult::Committed ==
        AutoRTFM::Transact([&] ()
        {
            AutoRTFM::DeferUntilCommit([&] () { bDidRun = true; });
            AutoRTFM::Open([&] () { REQUIRE(!bDidRun); });
        }));
    REQUIRE(bDidRun);
}
