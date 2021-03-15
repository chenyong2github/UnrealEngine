// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"
#include "Misc/AutomationTest.h"
#include "Tests/Benchmark.h"
#include "Tasks/Pipe.h"
#include "HAL/Thread.h"
#include "Async/ParallelFor.h"
#include "Experimental/Async/AwaitableTask.h"

#include <atomic>

#if WITH_DEV_AUTOMATION_TESTS

namespace UE { namespace TasksTests
{
	using namespace Tasks;

	void DummyFunc()
	{
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTasksBasicTest, "System.Core.Tasks.Basic", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter);

	void BasicStressTest()
	{
		constexpr uint32 SpawnerGroupsNum = 50;
		TArray<TTask<void>> SpawnerGroups;
		SpawnerGroups.Reserve(SpawnerGroupsNum);

		constexpr uint32 SpawnersPerGroupNum = 100;
		constexpr uint32 TasksNum = SpawnerGroupsNum * SpawnersPerGroupNum;
		TArray<TTask<void>> Spawners;
		Spawners.AddDefaulted(TasksNum);
		TArray<TTask<void>> Tasks;
		Tasks.AddDefaulted(TasksNum);

		std::atomic<uint32> TasksExecutedNum{ 0 };

		for (uint32 GroupIndex = 0; GroupIndex != SpawnerGroupsNum; ++GroupIndex)
		{
			SpawnerGroups.Add(Launch(UE_SOURCE_LOCATION,
				[
					Spawners = &Spawners[GroupIndex * SpawnersPerGroupNum],
					Tasks = &Tasks[GroupIndex * SpawnersPerGroupNum],
					&TasksExecutedNum,
					SpawnersPerGroupNum
				]
				{
					for (uint32 SpawnerIndex = 0; SpawnerIndex != SpawnersPerGroupNum; ++SpawnerIndex)
					{
						Spawners[SpawnerIndex] = Launch(UE_SOURCE_LOCATION,
							[
								Task = &Tasks[SpawnerIndex],
								&TasksExecutedNum
							]
							{
								*Task = Launch(UE_SOURCE_LOCATION,
									[&TasksExecutedNum]
									{
										++TasksExecutedNum;
									}
								);
							}
						);
					}
				}
			));
		}

		for (TTask<void>& SpawnerGroup : SpawnerGroups)
		{
			SpawnerGroup.Wait();
		}

		for (TTask<void>& Spawner : Spawners)
		{
			Spawner.Wait();
		}

		for (TTask<void>& Task : Tasks)
		{
			Task.Wait();
		}

		check(TasksExecutedNum == TasksNum);
	}

	bool FTasksBasicTest::RunTest(const FString& Parameters)
	{
		{	// basic example, fire and forget a high-pri task
			Tasks::Launch(
				UE_SOURCE_LOCATION, // debug name
				[] {}, // task body
				Tasks::ETaskPriority::High /* task priority, `Normal` by default */
			);
		}

		{	// launch a task and wait till it's executed
			Tasks::Launch(UE_SOURCE_LOCATION, [] {}).Wait();
		}

		{	// FTaskEvent
			FTaskEvent Event;
			check(!Event.IsTriggered());

			// check that waiting blocks
			TTask<void> Task = Launch(UE_SOURCE_LOCATION, [&Event] { Event.Wait(); });
			FPlatformProcess::Sleep(0.1f);
			check(!Task.IsCompleted());

			Event.Trigger();
			check(Event.IsTriggered());
			verify(Event.Wait(FTimespan::Zero()));
		}

		{	// postpone execution so waiting kicks in first
			std::atomic<int32> Counter{ 0 };
			TTask<void> Task = Launch(UE_SOURCE_LOCATION, [&Counter] { ++Counter; FPlatformProcess::Sleep(0.1f); });

			ensure(!Task.Wait(FTimespan::Zero()));
			Task.Wait();
			check(Counter == 1);
		}

		{	// same but using `FTaskEvent`
			FTaskEvent Event;
			TTask<void> Task = Launch(UE_SOURCE_LOCATION, [&Event] { Event.Wait(); });
			ensure(!Task.Wait(FTimespan::FromMilliseconds(100)));
			Event.Trigger();
			Task.Wait();
		}

		{	// basic use-case, postpone waiting so the task is executed first
			std::atomic<bool> Done{ false };
			TTask<void> Task = Launch(UE_SOURCE_LOCATION, [&Done] { Done = true; });
			while (!Task.IsCompleted())
			{
				FPlatformProcess::Yield();
			}
			Task.Wait();
			check(Done);
		}

		{	// basic use-case with result, postpone execution so waiting kicks in first
			TTask<int> Task = Launch(UE_SOURCE_LOCATION, [] { FPlatformProcess::Sleep(0.1f);  return 42; });
			verify(Task.GetResult() == 42);
		}

		{	// basic use-case with result, postpone waiting so the task is executed first
			TTask<int> Task = Launch(UE_SOURCE_LOCATION, [] { return 42; });
			while (!Task.IsCompleted())
			{
				FPlatformProcess::Yield();
			}
			verify(Task.GetResult() == 42);
		}

		{	// check that movable-only result types are supported, that only single instance of result is created and that it's destroyed
			static std::atomic<uint32> ConstructionsNum{ 0 }; // `static` to make it visible to `FMoveConstructable`
			static std::atomic<uint32> DestructionsNum{ 0 };

			struct FMoveConstructable
			{
				FORCENOINLINE FMoveConstructable()
				{
					++ConstructionsNum;
				}

				FMoveConstructable(FMoveConstructable&&)
					: FMoveConstructable()
				{
				}

				FMoveConstructable(const FMoveConstructable&) = delete;
				FMoveConstructable& operator=(FMoveConstructable&&) = delete;
				FMoveConstructable& operator=(const FMoveConstructable&) = delete;

				FORCENOINLINE ~FMoveConstructable()
				{
					++DestructionsNum;
				}
			};

			Launch(UE_SOURCE_LOCATION, [] { return FMoveConstructable{}; }).GetResult(); // consume the result
				
			checkf(ConstructionsNum == 1, TEXT("%d result instances were created but one was expected: the value stored in the task"));
			checkf(ConstructionsNum == DestructionsNum, TEXT("Mismatched number of constructions (%d) and destructions (%d)"));
		}

		// fire and forget: launch a task w/o keeping its reference
		if (LowLevelTasks::FScheduler::Get().GetNumWorkers() != 0)
		{
			std::atomic<bool> bDone{ false };
			Launch(UE_SOURCE_LOCATION, [&bDone] { bDone = true; });
			while (!bDone)
			{
				FPlatformProcess::Yield();
			}
		}

		{	// mutable lambda, compilation check
			Launch(UE_SOURCE_LOCATION, []() mutable {}).Wait();
			Launch(UE_SOURCE_LOCATION, []() mutable { return false; }).GetResult();
		}

		{	// free memory occupied by a private task instance, can be required if task instance is held as a member var
			TTask<void> Task = Launch(UE_SOURCE_LOCATION, [] {});
			Task.Wait();
			Task = {};
		}

		UE_BENCHMARK(5, BasicStressTest);

		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTasksPipeTest, "System.Core.Tasks.Pipe", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter);

	void PipeStressTest();

	bool FTasksPipeTest::RunTest(const FString& Parameters)
	{
		{	// a basic usage example
			Tasks::FPipe Pipe{ UE_SOURCE_LOCATION }; // a debug name, user-provided or 
				// `UE_SOURCE_LOCATION` - source file name and line number
			// launch two tasks in the pipe, they will be executed sequentially, but in parallel
			// with other tasks (including TaskGraph's old API tasks)
			Tasks::TTask<void> Task1 = Pipe.Launch(UE_SOURCE_LOCATION, [] {});
			Tasks::TTask<void> Task2 = Pipe.Launch(UE_SOURCE_LOCATION, [] {});
			Task2.Wait(); // wait for `Task2` completion
		}

		{	// an example of thread-safe async interface, kind of a primitive "actor"
			class FAsyncClass
			{
			public:
				TTask<bool> DoSomething()
				{
					return Pipe.Launch(TEXT("DoSomething()"), [this] { return DoSomethingImpl(); });
				}

				TTask<void> DoSomethingElse()
				{
					return Pipe.Launch(TEXT("DoSomethingElse()"), [this] { DoSomethingElseImpl(); });
				}

			private:
				bool DoSomethingImpl() { return false; }
				void DoSomethingElseImpl() {}

			private:
				FPipe Pipe{ UE_SOURCE_LOCATION };
			};

			// access the same instance from multiple threads
			FAsyncClass AsyncInstance;
			bool bRes = AsyncInstance.DoSomething().GetResult();
			AsyncInstance.DoSomethingElse().Wait();
		}

		{	// basic
			FPipe Pipe{ UE_SOURCE_LOCATION };
			Pipe.Launch(UE_SOURCE_LOCATION, [] {});
			Pipe.Launch(UE_SOURCE_LOCATION, [] {}).Wait();
		}

		{	// launching a piped task with pointer to a function
			FPipe Pipe{ UE_SOURCE_LOCATION };
			Pipe.Launch(UE_SOURCE_LOCATION, &DummyFunc).Wait();
		}

		{	// launching a piped task with a functor object
			struct FFunctor
			{
				void operator()()
				{
				}
			};

			FPipe Pipe{ UE_SOURCE_LOCATION };
			Pipe.Launch(UE_SOURCE_LOCATION, FFunctor{}).Wait();
		}

		{	// hold the first piped task execution until the next one is piped to test for non-concurrent execution
			FPipe Pipe{ UE_SOURCE_LOCATION };
			bool bTask1Done = false;
			TTask<void> Task1 = Pipe.Launch(UE_SOURCE_LOCATION, 
				[&bTask1Done] 
				{ 
					FPlatformProcess::Sleep(0.1f); 
					bTask1Done = true; 
				}
			);
			// we can't just check if `Task1` is completed because pipe gets unblocked and so the next piped task can start execution before the
			// previous piped task's comlpetion flag is set
			Pipe.Launch(UE_SOURCE_LOCATION, [&bTask1Done] { check(bTask1Done); }).Wait();
		}

		{	// piping another task after the previous one is completed and destroyed
			FPipe Pipe{ UE_SOURCE_LOCATION };

			Pipe.Launch(UE_SOURCE_LOCATION, [] {}).Wait();
			Pipe.Launch(UE_SOURCE_LOCATION, [] {}).Wait();
		}

		{	// an example of blocking a pipe
			FPipe Pipe{ UE_SOURCE_LOCATION };
			std::atomic<bool> bBlocked;
			FTaskEvent Event;
			TTask<void> Task = Pipe.Launch(UE_SOURCE_LOCATION,
				[&bBlocked, &Event]
				{
					bBlocked = true;
					Event.Wait(); 
				}
			);
			while (!bBlocked)
			{
			}
			// now it's blocked
			ensure(!Task.Wait(FTimespan::FromMilliseconds(100)));

			Event.Trigger(); // unblock
			Task.Wait();
		}

		UE_BENCHMARK(5, PipeStressTest);

		return true;
	}

	// stress test for named thread tasks, checks spawning a large number of tasks from multiple threads and executing them
	void PipeStressTest()
	{
		constexpr uint32 SpawnerGroupsNum = 50;
		TArray<TTask<void>> SpawnerGroups;
		SpawnerGroups.Reserve(SpawnerGroupsNum);

		constexpr uint32 SpawnersPerGroupNum = 100;
		constexpr uint32 TasksNum = SpawnerGroupsNum * SpawnersPerGroupNum;
		TArray<TTask<void>> Spawners;
		Spawners.AddDefaulted(TasksNum);
		TArray<TTask<void>> Tasks;
		Tasks.AddDefaulted(TasksNum);

		std::atomic<bool> bExecuting{ false };
		std::atomic<uint32> TasksExecutedNum{ 0 };

		FPipe Pipe{ UE_SOURCE_LOCATION };

		for (uint32 GroupIndex = 0; GroupIndex != SpawnerGroupsNum; ++GroupIndex)
		{
			SpawnerGroups.Add(Launch(UE_SOURCE_LOCATION,
				[
					Spawners = &Spawners[GroupIndex * SpawnersPerGroupNum],
					Tasks = &Tasks[GroupIndex * SpawnersPerGroupNum],
					&bExecuting,
					&TasksExecutedNum,
					SpawnersPerGroupNum,
					&Pipe
				]
				{
					for (uint32 SpawnerIndex = 0; SpawnerIndex != SpawnersPerGroupNum; ++SpawnerIndex)
					{
						Spawners[SpawnerIndex] = Launch(UE_SOURCE_LOCATION,
							[
								Task = &Tasks[SpawnerIndex],
								&bExecuting,
								&TasksExecutedNum,
								&Pipe
							]
							{
								*Task = Pipe.Launch(UE_SOURCE_LOCATION,
									[&bExecuting, &TasksExecutedNum]
									{
										check(!bExecuting);
										bExecuting = true;
										++TasksExecutedNum;
										bExecuting = false;
									}
								);
							}
						);
					}
				}
				));
		}

		for (TTask<void>& SpawnerGroup : SpawnerGroups)
		{
			SpawnerGroup.Wait();
		}

		for (TTask<void>& Spawner : Spawners)
		{
			Spawner.Wait();
		}

		for (TTask<void>& Task : Tasks)
		{
			Task.Wait();
		}

		check(TasksExecutedNum == TasksNum);
	}

	struct FAutoTlsSlot
	{
		uint32 Slot;

		FAutoTlsSlot()
			: Slot(FPlatformTLS::AllocTlsSlot())
		{
		}

		~FAutoTlsSlot()
		{
			FPlatformTLS::FreeTlsSlot(Slot);
		}
	};

	template<uint64 Num>
	void UeTlsStressTest()
	{
		static FAutoTlsSlot Slot;
		double Dummy = 0;
		for (uint64 i = 0; i != Num; ++i)
		{
			Dummy += (double)(uintptr_t)(FPlatformTLS::GetTlsValue(Slot.Slot));
			double Now = FPlatformTime::Seconds();
			FPlatformTLS::SetTlsValue(Slot.Slot, (void*)(uintptr_t)(Now));
		}
		FPlatformTLS::SetTlsValue(Slot.Slot, (void*)(uintptr_t)(Dummy));
	}

	template<uint64 Num>
	void ThreadLocalStressTest()
	{
		static thread_local double TlsValue;
		double Dummy = 0;
		for (uint64 i = 0; i != Num; ++i)
		{
			Dummy += TlsValue;
			double Now = FPlatformTime::Seconds();
			TlsValue = Now;
		}
		TlsValue = Dummy;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTlsTest, "System.Core.Tls", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter);

	bool FTlsTest::RunTest(const FString& Parameters)
	{
		UE_BENCHMARK(5, UeTlsStressTest<10000000>);
		UE_BENCHMARK(5, ThreadLocalStressTest<10000000>);

		return true;
	}
}}

#endif // WITH_DEV_AUTOMATION_TESTS
