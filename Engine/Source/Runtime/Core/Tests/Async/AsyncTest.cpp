// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"
#include "Templates/Function.h"
#include "Async/Async.h"
#include "TestFixtures/CoreTestFixture.h"

/** Helper methods used in the test cases. */
namespace AsyncTestUtils
{
	TFunction<int()> Task = [] {
		return 123;
	};

	bool bHasVoidTaskFinished = false;

	TFunction<void()> VoidTask = [] {
		bHasVoidTaskFinished = true;
	};
}


/** Test that task graph tasks return correctly. */
TEST_CASE_METHOD(FCoreTestFixture, "Core::Async::Task::Task Graph", "[Core][Async][Smoke]")
{
	auto Future = Async(EAsyncExecution::TaskGraph, AsyncTestUtils::Task);
	int Result = Future.Get();

	CHECK(Result==123);
}


/** Test that threaded tasks return correctly. */
TEST_CASE_METHOD(FCoreTestFixture, "Core::Async::Task::Thread", "[Core][Async][Smoke]")
{
	auto Future = Async(EAsyncExecution::Thread, AsyncTestUtils::Task);
	int Result = Future.Get();

	CHECK(Result==123);
}


/** Test that threaded pool tasks return correctly. */
TEST_CASE_METHOD(FCoreTestFixture, "Core::Async::Task::Threaded Pool", "[Core][Async][Smoke]")
{
	auto Future = Async(EAsyncExecution::ThreadPool, AsyncTestUtils::Task);
	int Result = Future.Get();

	CHECK(Result==123);
}


/** Test that void tasks run without errors or warnings. */
TEST_CASE_METHOD(FCoreTestFixture, "Core::Async::Task::Void Task", "[Core][Async][Smoke]")
{
	// Reset test variable before running
	AsyncTestUtils::bHasVoidTaskFinished = false;
	auto Future = Async(EAsyncExecution::TaskGraph, AsyncTestUtils::VoidTask);
	Future.Get();

	// Check that the variable state was updated by task
	CHECK(AsyncTestUtils::bHasVoidTaskFinished);
}


/** Test that FCoreTestFixture tasks have their completion callback called. */
TEST_CASE_METHOD(FCoreTestFixture, "Core::Async::Task::Completion Callback", "[Core][Async][Smoke]")
{
	bool Completed = false;
	FEvent* CompletedEvent = FPlatformProcess::GetSynchEventFromPool(true);

	auto Future = Async(EAsyncExecution::TaskGraph, AsyncTestUtils::Task, [&] {
		Completed = true;
		CompletedEvent->Trigger();
	});

	int Result = Future.Get();

	// We need an additional synchronization point here since the future get above will return after
	// the task is done but before the completion callback has returned!

	bool CompletedEventTriggered = CompletedEvent->Wait(FTimespan(0 /* hours */, 0 /* minutes */, 5 /* seconds */));
	FPlatformProcess::ReturnSynchEventToPool(CompletedEvent);

	CHECK(Result==123);
	CHECK(CompletedEventTriggered);
	CHECK(Completed);

}
