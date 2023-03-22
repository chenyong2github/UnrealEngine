// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_LOW_LEVEL_TESTS

#include "Async/ManualResetEvent.h"

#include "Tasks/Task.h"
#include "TestHarness.h"

namespace UE
{

TEST_CASE("Core::Async::ManualResetEvent", "[Core][Async]")
{
	FManualResetEvent Event;

	CHECK_FALSE(Event.WaitFor(FMonotonicTimeSpan::Zero()));
	Event.Notify();
	Event.Wait();
	CHECK(Event.WaitFor(FMonotonicTimeSpan::Zero()));
	CHECK(Event.WaitUntil(FMonotonicTimePoint::Now() - FMonotonicTimeSpan::FromSeconds(1.0)));
	Event.Reset();
	CHECK_FALSE(Event.WaitFor(FMonotonicTimeSpan::Zero()));

	Tasks::Launch(UE_SOURCE_LOCATION, [&Event] { Event.Notify(); });
	Event.Wait();

	Event.Notify();
	CHECK(Event.WaitFor(FMonotonicTimeSpan::Zero()));
	Event.Reset();
	CHECK_FALSE(Event.WaitFor(FMonotonicTimeSpan::Zero()));
}

} // UE

#endif // WITH_LOW_LEVEL_TESTS
