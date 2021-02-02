// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"
#include "Misc/AutomationTest.h"
#include "Async/TaskGraphInterfaces.h"
#include "Containers/CircularQueue.h"
#include "Containers/Queue.h"
#include "Containers/SpscQueue.h"
#include "Containers/MpscQueue.h"
#include "Tests/Benchmark.h"

#include <atomic>

#if WITH_DEV_AUTOMATION_TESTS

namespace ConcurrentQueuesTests
{
	template<typename QueueType>
	class TQueueAdapter : public QueueType
	{
	public:
		template<typename... ArgTypes>
		TQueueAdapter(ArgTypes... Args)
			: QueueType(Forward<ArgTypes>(Args)...)
		{
		}

		TOptional<typename QueueType::FElementType> Dequeue()
		{
			typename QueueType::FElementType Value;
			if (QueueType::Dequeue(Value))
			{
				return Value;
			}
			else
			{
				return {};
			}
		}
	};

	const uint32 CircularQueueSize = 1024;

	// measures performance of a queue when the producer and the consumer are run in the same thread
	template<uint32 Num, typename QueueType>
	void TestSpscQueueSingleThreadImp(QueueType& Queue)
	{
		const uint32 BatchSize = CircularQueueSize;
		const uint32 BatchNum = Num / BatchSize;
		for (uint32 BatchIndex = 0; BatchIndex != BatchNum; ++BatchIndex)
		{
			for (uint32 i = 0; i != BatchSize; ++i)
			{
				Queue.Enqueue(i);
			}

			for (uint32 i = 0; i != BatchSize; ++i)
			{
				TOptional<uint32> Consumed{ Queue.Dequeue() };
				checkSlow(Consumed.IsSet() && Consumed.GetValue() == i);
			}
		}
	}

	template<uint32 Num>
	void TestTCircularQueueSingleThread()
	{
		TQueueAdapter<TCircularQueue<uint32>> Queue{ CircularQueueSize + 1 };
		TestSpscQueueSingleThreadImp<Num>(Queue);
	}

	template<uint32 Num, typename QueueType>
	void TestQueueSingleThread()
	{
		QueueType Queue;
		TestSpscQueueSingleThreadImp<Num>(Queue);
	}

	template<uint32 Num, typename QueueType>
	void TestSpscQueue_Impl(QueueType& Queue)
	{
		std::atomic<bool> bStop{ false };

		FGraphEventRef Producer = FFunctionGraphTask::CreateAndDispatchWhenReady(
			[&bStop, &Queue]
			{
				while (!bStop)
				{
					Queue.Enqueue(0);
				}
			}
			);

		// consumer
		uint32 It = 0;
		while (It != Num)
		{
			if (Queue.Dequeue().IsSet())
			{
				++It;
			}
		}

		bStop = true;

		Producer->Wait(ENamedThreads::GameThread);
	}

	template<uint32 Num>
	void TestTCircularQueue()
	{
		TQueueAdapter<TCircularQueue<uint32>> Queue{ CircularQueueSize + 1 };
		TestSpscQueue_Impl<Num>(Queue);
	}

	template<uint32 Num, typename QueueType>
	void TestSpscQueue()
	{
		QueueType Queue;
		TestSpscQueue_Impl<Num>(Queue);
	}

	template<uint32 Num, typename QueueType>
	void TestSpscQueueCorrectness()
	{
		QueueType Queue;
		int32 NumProduced = 0;

		// producer
		FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady(
			[&Queue, &NumProduced]
			{
				do
				{
					Queue.Enqueue(0);
				} while (++NumProduced != Num);
			}
		);

		// consumer
		uint32 NumConcumed = 0;
		while (NumConcumed != Num)
		{
			if (Queue.Dequeue().IsSet())
			{
				++NumConcumed;
			}
		}

		while (Queue.Dequeue().IsSet())
		{
			++NumConcumed;
		}

		Task->Wait();

		check(NumProduced == NumConcumed);
	}

	template<uint32 Num, typename QueueType>
	void TestMpscQueue()
	{
		QueueType Queue;
		std::atomic<bool> bStop{ false };

		int32 NumProducers = FTaskGraphInterface::Get().GetNumWorkerThreads();
		FGraphEventArray Producers;
		for (int32 i = 0; i != NumProducers; ++i)
		{
			Producers.Add(FFunctionGraphTask::CreateAndDispatchWhenReady(
				[&bStop, &Queue]
				{
					while (!bStop)
					{
						Queue.Enqueue(0);
					}
				}
				));
		}

		uint32 It = 0;
		while (It != Num)
		{
			if (Queue.Dequeue().IsSet())
			{
				++It;
			}
		}

		bStop = true;

		FTaskGraphInterface::Get().WaitUntilTasksComplete(MoveTemp(Producers), ENamedThreads::GameThread);
	}

	template<uint32 Num, typename QueueType>
	void TestMpscQueueCorrectness()
	{
		struct alignas(PLATFORM_CACHE_LINE_SIZE) FCounter
		{
			uint32 Count = 0;
		};

		QueueType Queue;

		const int32 NumProducers = FTaskGraphInterface::Get().GetNumWorkerThreads();
		const uint32 NumPerProducer = Num / NumProducers;
		TArray<FCounter> NumsProduced;
		NumsProduced.AddDefaulted(NumProducers);
		for (int32 i = 0; i != NumProducers; ++i)
		{
			FFunctionGraphTask::CreateAndDispatchWhenReady(
				[&Queue, &NumsProduced, i, NumPerProducer]()
			{
				do
				{
					Queue.Enqueue(0);
				} while (++NumsProduced[i].Count < NumPerProducer);
			}
			);
		}

		// consumer
		uint32 NumConcumed = 0;
		while (NumConcumed != NumPerProducer * NumProducers)
		{
			if (Queue.Dequeue().IsSet())
			{
				++NumConcumed;
			}
		}

		while (Queue.Dequeue().IsSet())
		{
			++NumConcumed;
		}

		uint32 Produced = 0;
		for (int i = 0; i != NumProducers; ++i)
		{
			Produced += NumsProduced[i].Count;
		}
		check(Produced == NumConcumed);
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConcurrentQueuesTest, "System.Core.Async.ConcurrentQueuesTest", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter);

	bool FConcurrentQueuesTest::RunTest(const FString& Parameters)
	{
		{	// test for support of not default constructible types
			struct FNonDefaultConstructable
			{
				int Value;

				explicit FNonDefaultConstructable(int InValue)
					: Value(InValue)
				{
					UE_LOG(LogTemp, Display, TEXT("ctor"));
				}

				~FNonDefaultConstructable()
				{
					UE_LOG(LogTemp, Display, TEXT("dctor"));
				}

				FNonDefaultConstructable(const FNonDefaultConstructable&) = delete;
				FNonDefaultConstructable& operator=(const FNonDefaultConstructable&) = delete;

				FNonDefaultConstructable(FNonDefaultConstructable&& Other)
					: Value(Other.Value)
				{
					UE_LOG(LogTemp, Display, TEXT("move-ctor"));
				}

				FNonDefaultConstructable& operator=(FNonDefaultConstructable&& Other)
				{
					Value = Other.Value;
					UE_LOG(LogTemp, Display, TEXT("move="));
					return *this;
				}
			};

			{
				TSpscQueue<FNonDefaultConstructable> Q;
				Q.Enqueue(1);
				TOptional<FNonDefaultConstructable> Res{ Q.Dequeue() };
				verify(Res.IsSet() && Res.GetValue().Value == 1);
			}

			{
				TMpscQueue<FNonDefaultConstructable> Q;
				Q.Enqueue(1);
				TOptional<FNonDefaultConstructable> Res{ Q.Dequeue() };
				verify(Res.IsSet() && Res.GetValue().Value == 1);
			}
		}

		{	// test queue destruction and not default movable types
			struct FNonTrivial
			{
				int* Value;

				explicit FNonTrivial(TUniquePtr<int> InValue)
					: Value(InValue.Release())
				{}

				~FNonTrivial()
				{
					// check for double delete
					check(Value != (int*)0xcdcdcdcdcdcdcdcd);
					delete Value;
					Value = (int*)0xcdcdcdcdcdcdcdcd;
				}

				FNonTrivial(const FNonTrivial&) = delete;
				FNonTrivial& operator=(const FNonTrivial&) = delete;

				FNonTrivial(FNonTrivial&& Other)
				{
					Value = Other.Value;
					Other.Value = nullptr;
				}

				FNonTrivial& operator=(FNonTrivial&& Other)
				{
					Swap(Value, Other.Value);
					return *this;
				}
			};

			// SPSC

			{	// destroy queue while it's holding one unconsumed item
				TSpscQueue<FNonTrivial> Q;
				Q.Enqueue(MakeUnique<int>(1));
			}

			{	// destroy queue while it's holding one cached consumed time
				TSpscQueue<FNonTrivial> Q;
				Q.Enqueue(MakeUnique<int>(1));
				TOptional<FNonTrivial> Res{ Q.Dequeue() };
				verify(Res.IsSet() && *Res.GetValue().Value == 1);
			}

			{	// destroy queue while it's holding one chached consumed item and one unconsumed item
				TSpscQueue<FNonTrivial> Q;
				Q.Enqueue(MakeUnique<int>(1));
				Q.Enqueue(MakeUnique<int>(2));
				TOptional<FNonTrivial> Res{ Q.Dequeue() };
				verify(Res.IsSet() && *Res.GetValue().Value == 1);
			}

			// MPSC
			{	// destroy untouched queue
				TMpscQueue<FNonTrivial> Q;
			}

			{	// destroy never consumed queue with one unconsumed item
				TMpscQueue<FNonTrivial> Q;
				Q.Enqueue(MakeUnique<int>(1));
			}

			{	// destroy empty queue
				TMpscQueue<FNonTrivial> Q;
				Q.Enqueue(MakeUnique<int>(1));
				TOptional<FNonTrivial> Res{ Q.Dequeue() };
				verify(Res.IsSet() && *Res.GetValue().Value == 1);
			}

			{	// destroy queue with one unconsumed item
				TMpscQueue<FNonTrivial> Q;
				Q.Enqueue(MakeUnique<int>(1));
				Q.Enqueue(MakeUnique<int>(2));
				TOptional<FNonTrivial> Res{ Q.Dequeue() };
				verify(Res.IsSet() && *Res.GetValue().Value == 1);
			}

			{	// destroy queue with two items
				TMpscQueue<FNonTrivial> Q;
				Q.Enqueue(MakeUnique<int>(1));
				Q.Enqueue(MakeUnique<int>(2));
			}

			{	// enqueue and dequeue multiple items
				TMpscQueue<FNonTrivial> Q;
				Q.Enqueue(MakeUnique<int>(1));
				Q.Enqueue(MakeUnique<int>(2));
				TOptional<FNonTrivial> Res{ Q.Dequeue() };
				verify(Res.IsSet() && *Res.GetValue().Value == 1);
				Res = Q.Dequeue();
				verify(Res.IsSet() && *Res.GetValue().Value == 2);
			}

			{	// enqueue and dequeue (interleaved) multiple items
				TMpscQueue<FNonTrivial> Q;
				Q.Enqueue(MakeUnique<int>(1));
				TOptional<FNonTrivial> Res{ Q.Dequeue() };
				verify(Res.IsSet() && *Res.GetValue().Value == 1);
				Q.Enqueue(MakeUnique<int>(2));
				Res = Q.Dequeue();
				verify(Res.IsSet() && *Res.GetValue().Value == 2);
			}
		}

		UE_BENCHMARK(5, TestTCircularQueueSingleThread<5'000'000>);
		UE_BENCHMARK(5, TestQueueSingleThread<5'000'000, TQueueAdapter<TQueue<uint32, EQueueMode::Spsc>>>);
		UE_BENCHMARK(5, TestQueueSingleThread<5'000'000, TQueueAdapter<TQueue<uint32, EQueueMode::Mpsc>>>);
		UE_BENCHMARK(5, TestQueueSingleThread<5'000'000, TMpscQueue<uint32>>);
		UE_BENCHMARK(5, TestQueueSingleThread<5'000'000, TSpscQueue<uint32>>);

		UE_BENCHMARK(5, TestSpscQueueCorrectness<5'000'000, TMpscQueue<uint32>>);
		UE_BENCHMARK(5, TestSpscQueueCorrectness<5'000'000, TSpscQueue<uint32>>);

		UE_BENCHMARK(5, TestTCircularQueue<5'000'000>);
		UE_BENCHMARK(5, TestSpscQueue<5'000'000, TQueueAdapter<TQueue<uint32, EQueueMode::Spsc>>>);
		UE_BENCHMARK(5, TestSpscQueue<5'000'000, TQueueAdapter<TQueue<uint32, EQueueMode::Mpsc>>>);
		UE_BENCHMARK(5, TestSpscQueue<5'000'000, TMpscQueue<uint32>>);
		UE_BENCHMARK(5, TestSpscQueue<5'000'000, TSpscQueue<uint32>>);

		UE_BENCHMARK(5, TestMpscQueueCorrectness<5'000'000, TQueueAdapter<TQueue<uint32, EQueueMode::Mpsc>>>);
		UE_BENCHMARK(5, TestMpscQueueCorrectness<5'000'000, TMpscQueue<uint32>>);

		UE_BENCHMARK(5, TestMpscQueue<1'000'000, TQueueAdapter<TQueue<uint32, EQueueMode::Mpsc>>>);
		UE_BENCHMARK(5, TestMpscQueue<1'000'000, TMpscQueue<uint32>>);

		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
