// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"
#include "Containers/Queue.h"
#include "TestHarness.h"

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Misc::TQueue::Smoke Test", "[Core][Misc][Smoke]")
{
	// empty queues
	{
		TQueue<int32> Queue;
		int32 OutItem = 0;

		TEST_TRUE(TEXT("A new queue must be empty"), Queue.IsEmpty());

		TEST_FALSE(TEXT("A new queue must not dequeue anything"), Queue.Dequeue(OutItem));
		TEST_FALSE(TEXT("A new queue must not peek anything"), Queue.Peek(OutItem));
	}

	// insertion & removal
	{
		TQueue<int32> Queue;
		int32 Item1 = 1;
		int32 Item2 = 2;
		int32 Item3 = 3;
		int32 OutItem = 0;

		TEST_TRUE(TEXT("Inserting into a new queue must succeed"), Queue.Enqueue(Item1));
		TEST_TRUE(TEXT("Peek must succeed on a queue with one item"), Queue.Peek(OutItem));
		TEST_EQUAL(TEXT("Peek must return the first value"), OutItem, Item1);

		TEST_TRUE(TEXT("Inserting into a non-empty queue must succeed"), Queue.Enqueue(Item2));
		TEST_TRUE(TEXT("Peek must succeed on a queue with two items"), Queue.Peek(OutItem));
		TEST_EQUAL(TEXT("Peek must return the first item"), OutItem, Item1);

		Queue.Enqueue(Item3);

		TEST_TRUE(TEXT("Dequeue must succeed on a queue with three items"), Queue.Dequeue(OutItem));
		TEST_EQUAL(TEXT("Dequeue must return the first item"), OutItem, Item1);
		TEST_TRUE(TEXT("Dequeue must succeed on a queue with two items"), Queue.Dequeue(OutItem));
		TEST_EQUAL(TEXT("Dequeue must return the second item"), OutItem, Item2);
		TEST_TRUE(TEXT("Dequeue must succeed on a queue with one item"), Queue.Dequeue(OutItem));
		TEST_EQUAL(TEXT("Dequeue must return the third item"), OutItem, Item3);

		TEST_TRUE(TEXT("After removing all items, the queue must be empty"), Queue.IsEmpty());
	}

	// emptying
	{
		TQueue<int32> Queue;

		Queue.Enqueue(1);
		Queue.Enqueue(2);
		Queue.Enqueue(3);
		Queue.Empty();

		TEST_TRUE(TEXT("An emptied queue must be empty"), Queue.IsEmpty());
	}
}
