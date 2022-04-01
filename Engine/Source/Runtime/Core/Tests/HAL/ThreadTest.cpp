// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"
#include "Misc/AssertionMacros.h"
#include "HAL/Thread.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/ThreadSafeCounter.h"
#include "HAL/ThreadSingleton.h"
#include "HAL/Event.h"
#include "Containers/Queue.h"
#include "Containers/StringConv.h"
#include "TestFixtures/CoreTestFixture.h"

namespace
{
	void TestIsJoinableAfterCreation(FAutomationTestFixture& Test)
	{
		FThread Thread(TEXT("Test.Thread.TestIsJoinableAfterCreation"), []() { /*NOOP*/ });
		INFO("FThread must be joinable after construction");
		CHECK(Thread.IsJoinable());
		Thread.Join();
		UE_LOG(LogTemp, Log, TEXT("%s completed"), StringCast<TCHAR>(__FUNCTION__).Get());
	}

	void TestIsJoinableAfterCompletion(FAutomationTestFixture& Test)
	{
		FThreadSafeBool bDone = false;
		FThread Thread(TEXT("Test.Thread.TestIsJoinableAfterCompletion"), [&bDone]() { bDone = true; });
		while (!bDone) {}; // wait for completion //-V529
		INFO("FThread must still be joinable after completion");
		CHECK(Thread.IsJoinable());
		Thread.Join();
		UE_LOG(LogTemp, Log, TEXT("%s completed"), StringCast<TCHAR>(__FUNCTION__).Get());
	}

	void TestIsNotJoinableAfterJoining(FAutomationTestFixture& Test)
	{
		FThread Thread(TEXT("Test.Thread.TestIsNotJoinableAfterJoining"), []() { /*NOOP*/ });
		Thread.Join();
		INFO("FThread must not be joinable after joining");
		CHECK_FALSE(Thread.IsJoinable());
		UE_LOG(LogTemp, Log, TEXT("%s completed"), StringCast<TCHAR>(__FUNCTION__).Get());
	}

#if 0 // detaching is not implemented
	void TestIsNotJoinableAfterDetaching(FAutomationTestFixture& Test)
	{
		// two cases: it's either the calling thread detaches from the thread before the thread is completed
		{
			TAtomic<bool> bReady{ false };
			FThread Thread(TEXT("Test.Thread"), [&bReady]()
				{
					while (!bReady) {}
				});
			Thread.Detach();
			bReady = true; // make sure `Detach` is called before thread function exit
			INFO("FThread must not be joinable after detaching");
			CHECK_FALSE(Thread.IsJoinable());
		}
		// or thread function is completed fast and `FThreadImpl` releases the reference to itself before
		// `Detach` call
		{
			TAtomic<bool> bReady{ false };
			FThread Thread(TEXT("Test.Thread"), [&bReady]() { /*NOOP*/});
			FPlatformProcess::Sleep(0.1); // let the thread exit before detaching
			Thread.Detach();
			bReady = true; // make sure `Detach` is called before thread function exit
			INFO("FThread must not be joinable after detaching");
			CHECK_FALSE(Thread.IsJoinable());
		}
		UE_LOG(LogTemp, Log, TEXT("%s completed"), StringCast<TCHAR>(__FUNCTION__).Get());
	}
#endif

	void TestAssertIfNotJoinedOrDetached(FAutomationTestFixture& Test)
	{
		// this does fails the `check`, but it seems there's no way to test this by UE4 Automation Testing, so commented out
		FThread Thread(TEXT("Test.Thread.TestAssertIfNotJoinedOrDetached"), []() { /*NOOP*/ });
		// should assert in the destructor
	}

	void TestDefaultConstruction(FAutomationTestFixture& Test)
	{
		{
			FThread Thread;
			INFO("Default-constructed FThread must be not joinable");
			CHECK_FALSE(Thread.IsJoinable());
		}
		{	// check that default constructed thread can be "upgraded" to joinable thread
			FThread Thread;
			Thread = FThread(TEXT("Test.Thread.TestDefaultConstruction"), []() { /* NOOP */ });
			INFO("Move-constructed FThread from joinable thread must be joinable");
			CHECK(Thread.IsJoinable());
			Thread.Join();
		}
		UE_LOG(LogTemp, Log, TEXT("%s completed"), StringCast<TCHAR>(__FUNCTION__).Get());
	}

	void TestThreadSingleton(FAutomationTestFixture& Test)
	{
		{	// check that ThreadSingleton instances in different threads are isolated from each other
			class FThreadSingletonTest : public TThreadSingleton<FThreadSingletonTest>
			{
			public:
				void SetTestField(int NewValue) { TestField = NewValue; }
				int GetTestField() const { return TestField; }
			private:
				int TestField{ 0 };
			};
			FThreadSingletonTest::Get().SetTestField(1);
			FThread Thread;
			TAtomic<bool> bDefaultValuePass{ false };
			Thread = FThread(TEXT("Test.Thread.TestThreadSingleton"),
				[&bDefaultValuePass]()
				{
					bDefaultValuePass = FThreadSingletonTest::TryGet() == nullptr;
					bDefaultValuePass = bDefaultValuePass && (FThreadSingletonTest::Get().GetTestField() == 0);
					FThreadSingletonTest::Get().SetTestField(2);
				});
			Thread.Join();

			CHECK(bDefaultValuePass);
			CHECK(FThreadSingletonTest::Get().GetTestField() == 1);
		}
		{	// check that ThreadSingleton entries don't point to invalid memory after cleanup
			class FThreadSingletonFirst : public TThreadSingleton<FThreadSingletonFirst>
			{
			};
			class FThreadSingletonSecond : public TThreadSingleton<FThreadSingletonSecond>
			{
			public:
				virtual ~FThreadSingletonSecond()
				{
					// By the time we reach this destructor, the first singleton's destructor should have been executed already.
					check(FThreadSingletonFirst::TryGet() == nullptr);
				}
			};
			FThread Thread;
			Thread = FThread(TEXT("Test.Thread.TestThreadSingleton"),
				[]()
				{
					FThreadSingletonFirst::Get();
					FThreadSingletonSecond::Get();
				});
			Thread.Join();
			CHECK(FThreadSingletonFirst::TryGet() == nullptr);
			CHECK(FThreadSingletonSecond::TryGet() == nullptr);
		}
		UE_LOG(LogTemp, Log, TEXT("%s completed"), StringCast<TCHAR>(__FUNCTION__).Get());
	}

	void TestMovability(FAutomationTestFixture& Test)
	{
		{	
			INFO("move constructor with default - constructed thread");
			FThread Src;
			FThread Dst(MoveTemp(Src));
			CHECK_FALSE( Src.IsJoinable());
			CHECK_FALSE(Dst.IsJoinable());
		}
		{	
			INFO("move constructor with joinable thread");
			FThread Src(TEXT("Test.Thread.TestMovability.1"), []() { /* NOOP */ });
			FThread Dst(MoveTemp(Src));
			CHECK_FALSE(Src.IsJoinable());
			CHECK(Dst.IsJoinable());
			Dst.Join();
		}
		{	
			INFO("move assignment operator");
			FThread Src(TEXT("Test.Thread.TestMovability.2"), []() { /* NOOP */ });
			FThread Dst;
			Dst = MoveTemp(Src);
			CHECK_FALSE(Src.IsJoinable());
			CHECK(Dst.IsJoinable());
			Dst.Join();
		}
		{	// Failure test for move assignment operator of joinable thread
			//FThread Src(TEXT("Test.Thread"), []() { /* NOOP */ });
			//FThread Dst(TEXT("Test.Thread"), []() { /* NOOP */ });
			//Dst = MoveTemp(Src); // must assert that joinable thread wasn't joined before move-assignment, no way to test this
			//Dst.Join();
		}
		{	
			INFO("Move assignment operator of thread that has been joined");
			FThread Src(TEXT("Test.Thread.TestMovability.3"), []() { /* NOOP */ });
			FThread Dst(TEXT("Test.Thread.TestMovability.4"), []() { /* NOOP */ });
			Dst.Join();
			Dst = MoveTemp(Src);
			Dst.Join();
		}
		UE_LOG(LogTemp, Log, TEXT("%s completed"), StringCast<TCHAR>(__FUNCTION__).Get());
	}

	// An example of possible implementation of Consumer/Producer idiom
	void TestTypicalUseCase(FAutomationTestFixture& Test)
	{
		FThreadSafeBool bQuitRequested = false;
		using FWork = uint32;
		TQueue<FWork> WorkQueue;
		FEvent* WorkQueuedEvent = FPlatformProcess::GetSynchEventFromPool();

		FThread WorkerThread(TEXT("Test.Thread.TestTypicalUseCase"), [&bQuitRequested, &WorkQueue, WorkQueuedEvent]() 
		{
			while (!bQuitRequested)
			{
				// get work
				FWork Work;
				if (!WorkQueue.Dequeue(Work))
				{
					WorkQueuedEvent->Wait();
					// first check if quitting requested
					continue;
				}

				// do work
				UE_LOG(LogTemp, Log, TEXT("Work #%d consumed"), Work);
			}

			UE_LOG(LogTemp, Log, TEXT("Quit"));
		});

		// produce work
		const int WorkNum = 3;
		for (FWork Work = 0; Work != WorkNum; ++Work)
		{
			WorkQueue.Enqueue(Work);
			WorkQueuedEvent->Trigger();
			UE_LOG(LogTemp, Log, TEXT("Work #%d produced"), Work);
		}

		UE_LOG(LogTemp, Log, TEXT("Request to quit"));
		bQuitRequested = true;
		// the thread can be blocked waiting for work, unblock it
		WorkQueuedEvent->Trigger();
		WorkerThread.Join();

		FPlatformProcess::ReturnSynchEventToPool(WorkQueuedEvent);

		// example of output:
		// Work #0 produced
		//	Work #0 consumed
		//	Work #1 produced
		//	Work #1 consumed
		//	Work #2 produced
		//	Work #2 consumed
		//	Request to quit
		//	The thread 0x96e0 has exited with code 0 (0x0).
		//	Quit
	
		UE_LOG(LogTemp, Log, TEXT("%s completed"), StringCast<TCHAR>(__FUNCTION__).Get());
	}
}

TEST_CASE_METHOD(FCoreTestFixture, "Core::HAL::Thread::Smoke Test", "[Core][HAL][Smoke]")
{
	UE_LOG(LogTemp, Log, TEXT("%s"), StringCast<TCHAR>(__FUNCTION__).Get());

	TestIsJoinableAfterCreation(*this);
	TestIsJoinableAfterCompletion(*this);
	TestIsNotJoinableAfterJoining(*this);
	
#if 0 // detaching is not implemented
	TestIsNotJoinableAfterDetaching(*this);
#endif

	//TestAssertIfNotJoinedOrDetached();

	TestDefaultConstruction(*this);
	TestMovability(*this);
	TestTypicalUseCase(*this);
	TestThreadSingleton(*this);
}
