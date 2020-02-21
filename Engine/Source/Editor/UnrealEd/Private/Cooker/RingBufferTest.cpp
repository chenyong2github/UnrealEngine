// Copyright Epic Games, Inc. All Rights Reserved.

#include "RingBuffer.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

class FRingBufferTest : FAutomationTestBase
{
public:
	static bool IsIntegerRange(const TRingBuffer<uint32>& Queue, uint32 Start, uint32 End, bool bForward = true)
	{
		if (Queue.Num() != End - Start)
		{
			return false;
		}

		// Peek elements in queue at given offset, peek from back to front
		for (int32 It = 0; It < Queue.Num(); ++It)
		{
			uint32 QueueValue = bForward ? Queue[It] : Queue[Queue.Num() - 1 - It];
			if (QueueValue != Start + It)
			{
				return false;
			}
		}

		return true;
	}

	struct Counter
	{
		Counter(uint32 InValue = 0x12345)
			:Value(InValue)
		{
			++NumVoid;
		}
		Counter(const Counter& Other)
			:Value(Other.Value)
		{
			++NumCopy;
		}
		Counter(Counter&& Other)
			:Value(Other.Value)
		{
			++NumMove;
		}
		~Counter()
		{
			++NumDestruct;
		}

		operator uint32() const
		{
			return Value;
		}

		bool operator==(const Counter& Other) const
		{
			return Value == Other.Value;
		}
		static int NumVoid;
		static int NumCopy;
		static int NumMove;
		static int NumDestruct;
		static void Clear()
		{
			NumVoid = NumCopy = NumMove = NumDestruct = 0;
		}

		uint32 Value;
	};

	FRingBufferTest(const FString& InName, const bool bInComplexTask)
		: FAutomationTestBase(InName, bInComplexTask)
	{
	}

	bool RunTest(const FString& Parameters)
	{
		// Test empty
		{
			TRingBuffer<uint32> Q(0);

			TestTrue(TEXT("Test empty - IsEmpty"), Q.IsEmpty());
			TestEqual(TEXT("Test empty - Size"), Q.Num(), 0);
			TestEqual(TEXT("Test empty - Capacity"), Q.GetCapacity(), 0);
			TestEqual(TEXT("Test empty - Iterator"), Q.begin(), Q.end());
			TestEqual(TEXT("Test empty - ConvertReferenceToIndex"), Q.ConvertReferenceToIndex(0), INDEX_NONE);
			TestEqual(TEXT("Test empty - ConvertReferenceToIndex"), Q.ConvertReferenceToIndex(1), INDEX_NONE);
			Q.Trim();
			TestEqual(TEXT("Test Trim From empty - Size"), Q.Num(), 0);
			TestEqual(TEXT("Test Trim From empty - Capacity"), Q.GetCapacity(), 0);
			Q.Reset();
			TestEqual(TEXT("Test Reset From empty - Size"), Q.Num(), 0);
			TestEqual(TEXT("Test Reset From empty - Capacity"), Q.GetCapacity(), 0);
			Q.Empty(0);
			TestEqual(TEXT("Test Empty From empty - Size"), Q.Num(), 0);
			TestEqual(TEXT("Test Empty From empty - Capacity"), Q.GetCapacity(), 0);
			Q.PopFront(0);
			Q.PopBack(0);
			TestEqual(TEXT("Test Pop on empty - Size"), Q.Num(), 0);
			TestEqual(TEXT("Test Pop on empty - Capacity"), Q.GetCapacity(), 0);
			TestEqual(TEXT("Test empty - IsValidIndex"), Q.IsValidIndex(0), false);


			const TRingBuffer<uint32> ConstQ(0);
			TestTrue(TEXT("Test const empty - IsEmpty"), ConstQ.IsEmpty());
			TestEqual(TEXT("Test const empty - Size"), ConstQ.Num(), 0);
			TestEqual(TEXT("Test const empty - Capacity"), ConstQ.GetCapacity(), 0);
			TestEqual(TEXT("Test const empty - Iterator"), ConstQ.begin(), ConstQ.end());
			TestEqual(TEXT("Test const empty - ConvertReferenceToIndex"), ConstQ.ConvertReferenceToIndex(0), INDEX_NONE);
		}

		// Test Push Sequence
		{
			const TRingBuffer<int32>::IndexType FirstSize = 8;

			TRingBuffer<int32> Q(0);

			TestEqual(TEXT("Test PushSequence - Capacity (Implementation Detail)"), Q.GetCapacity(), 0);
			Q.EmplaceBack(0);
			TestEqual(TEXT("Test PushSequence - Size"), Q.Num(), 1);
			TestEqual(TEXT("Test PushSequence - Capacity (Implementation Detail)"), Q.GetCapacity(), 1);
			Q.EmplaceBack(1);
			TestEqual(TEXT("Test PushSequence - Size"), Q.Num(), 2);
			TestEqual(TEXT("Implementation Detail - These tests expect that growing size will set capacity to successive powers of 2."), Q.GetCapacity(), 2);
			for (int32 It = 2; It < FirstSize; ++It)
			{
				Q.EmplaceBack(It);
				TestEqual(TEXT("Test PushSequence - Size"), Q.Num(), It + 1);
				TestEqual(TEXT("Test PushSequence - Capacity (Implementation Detail)"), static_cast<uint32>(Q.GetCapacity()), FMath::RoundUpToPowerOfTwo(It + 1));
			}

			for (int32 Index = 0; Index < FirstSize; ++Index)
			{
				TestEqual(TEXT("Test PushSequence - Expected values"), Q[Index], Index);
				TestEqual(TEXT("Test PushSequence const- Expected values"), const_cast<TRingBuffer<int32>&>(Q)[Index], Index);
			}

			const TRingBuffer<int32>::IndexType SecondSize = 13;
			for (int32 It = FirstSize; It < SecondSize; ++It)
			{
				Q.EmplaceBack(It);
				TestEqual(TEXT("Test PushSequence non powerof2 - Size"), Q.Num(), It + 1);
				TestEqual(TEXT("Test PushSequence non powerof2 const - Capacity (Implementation Detail)"), static_cast<uint32>(Q.GetCapacity()), FMath::RoundUpToPowerOfTwo(It + 1));
			}

			for (int32 Index = 0; Index < FirstSize; ++Index)
			{
				TestEqual(TEXT("Test PushSequence non powerof2 - Expected values"), Q[Index], Index);
				TestEqual(TEXT("Test PushSequence non powerof2 const - Expected values"), const_cast<TRingBuffer<int32>&>(Q)[Index], Index);
			}
		}

		// Test Push under/over Capacity
		{
			const TRingBuffer<int32>::IndexType FirstElementsToPush = 3;
			const TRingBuffer<int32>::IndexType InitialCapacity = 8;
			const TRingBuffer<int32>::IndexType SecondElementsToPush = 9;

			TRingBuffer<int32> Q(InitialCapacity);

			for (int32 It = 0; It < FirstElementsToPush; ++It)
			{
				Q.EmplaceBack(It);
			}

			TestEqual(TEXT("Test Push under Capacity - Size"), Q.Num(), FirstElementsToPush);
			TestEqual(TEXT("Test Push under Capacity - Capacity"), Q.GetCapacity(), InitialCapacity);
			for (int32 Index = 0; Index < FirstElementsToPush; ++Index)
			{
				TestEqual(TEXT("Test Push under Capacity - Expected values"), Q[Index], Index);
				TestEqual(TEXT("Test Push under Capacity const - Expected values"), const_cast<TRingBuffer<int32>&>(Q)[Index], Index);
			}

			for (int32 It = FirstElementsToPush; It < SecondElementsToPush; ++It)
			{
				Q.EmplaceBack(It);
			}

			TestEqual(TEXT("Test Push over Capacity - Size"), Q.Num(), SecondElementsToPush);
			TestEqual(TEXT("Test Push over Capacity - Capacity (Implementation Detail)"), static_cast<uint32>(Q.GetCapacity()), FMath::RoundUpToPowerOfTwo(SecondElementsToPush));
			for (int32 Index = 0; Index < SecondElementsToPush; ++Index)
			{
				TestEqual(TEXT("Test Push over Capacity - Expected values"), Q[Index], Index);
				TestEqual(TEXT("Test Push over Capacity const - Expected values"), const_cast<TRingBuffer<int32>&>(Q)[Index], Index);
			}
		}

		// Test GetBack/GetFront
		{
			TRingBuffer<uint32> Q({ 0,1,2,3 });
			TestEqual(TEXT("Test GetBack"), 3, Q.GetBack());
			Q.GetBack() = 4;
			TestEqual(TEXT("Test GetBack const"), 4, const_cast<TRingBuffer<uint32>&>(Q).GetBack());
			TestEqual(TEXT("Test GetFront"), 0, Q.GetFront());
			Q.GetFront() = 5;
			TestEqual(TEXT("Test GetFront const"), 5, const_cast<TRingBuffer<uint32>&>(Q).GetFront());
		}

		// Test PopFrontValue/PopBackValue
		{
			TRingBuffer<Counter> Q({ 31,32,33 });
			Q.PushFront(30);

			Counter::Clear();
			Counter C(Q.PopFrontValue());
			TestEqual(TEXT("PopFrontValue - PoppedValue"), C.Value, 30);
			TestTrue(TEXT("PopFrontValue - ConstructorCounts"), Counter::NumMove > 0 && Counter::NumCopy == 0);
			TestEqual(TEXT("PopFrontValue - Remaining Values"), Q, TRingBuffer<Counter>({ 31,32,33 }));
			Counter::Clear();
			TestEqual(TEXT("PopFrontValue Inline - PoppedValue"), Q.PopFrontValue().Value, 31);
			TestTrue(TEXT("PopFrontValue Inline - ConstructorCounts"), Counter::NumCopy == 0);
			TestEqual(TEXT("PopFrontValue Inline - Remaining Values"), Q, TRingBuffer<Counter>({ 32,33 }));

			Counter::Clear();
			Counter D(Q.PopBackValue());
			TestEqual(TEXT("PopBackValue - PoppedValue"), D.Value, 33);
			TestTrue(TEXT("PopBackValue - ConstructorCounts"), Counter::NumMove > 0 && Counter::NumCopy == 0);
			TestEqual(TEXT("PopBackValue - Remaining Values"), Q, TRingBuffer<Counter>({ Counter(32) }));
			Counter::Clear();
			TestEqual(TEXT("PopBackValue Inline - PoppedValue"), Q.PopBackValue().Value, 32);
			TestTrue(TEXT("PopBackValue Inline - ConstructorCounts"), Counter::NumCopy == 0);
			TestTrue(TEXT("PopBackValue Inline - Remaining Values"), Q.IsEmpty());
		}

		// Test Initializer_List
		{
			const TRingBuffer<int32>::IndexType InitializerSize = 9;
			TRingBuffer<int32> Q({ 0, 1, 2, 3, 4, 5, 6, 7, 8 });

			TestEqual(TEXT("Test Initializer_List - Size"), Q.Num(), InitializerSize);
			TestEqual(TEXT("Test Initializer_List - Capacity (Implementation Detail)"), static_cast<uint32>(Q.GetCapacity()), FMath::RoundUpToPowerOfTwo(InitializerSize));
			for (int32 Index = 0; Index < InitializerSize; ++Index)
			{
				TestEqual(TEXT("Test Initializer_List - Expected values"), Q[Index], Index);
			}
		}

		// Test RingBuffer's Copy Constructors et al
		{
			TRingBuffer<uint32> Original({ 0,1,2,3,4,5,6,7 });
			TRingBuffer<uint32> Copy(Original);
			TestEqual(TEXT("Copy Constructor"), Original, Copy);
			TRingBuffer<uint32> Moved(MoveTemp(Copy));
			TestEqual(TEXT("Move Constructor"), Original, Moved);
			TestEqual(TEXT("Move Constructor did in fact move"), Copy.GetCapacity(), 0);
			TRingBuffer<uint32> AssignCopy;
			AssignCopy = Original;
			TestEqual(TEXT("Copy Assignment"), Original, AssignCopy);
			TRingBuffer<uint32> AssignMove;
			AssignMove = MoveTemp(AssignCopy);
			TestEqual(TEXT("Move Assignment"), Original, AssignMove);
			TestEqual(TEXT("Move Assignment did in fact move"), AssignCopy.GetCapacity(), 0);
		}

		// Test Equality 
		{
			auto TestEquality = [this](const TCHAR* Message, bool ExpectedEqual, const TRingBuffer<int32>& A, const TRingBuffer<int32>& B)
			{
				TestEqual(*FString::Printf(TEXT("Test equality - %s - A == B"), Message), A == B, ExpectedEqual);
				TestEqual(*FString::Printf(TEXT("Test equality - %s - B == A"), Message), B == A, ExpectedEqual);
				TestEqual(*FString::Printf(TEXT("Test equality - %s - A != B"), Message), A != B, !ExpectedEqual);
				TestEqual(*FString::Printf(TEXT("Test equality - %s - B != A"), Message), B != A, !ExpectedEqual);
			};

			TestEquality(TEXT("empty"), true, TRingBuffer<int32>(0), TRingBuffer<int32>(0));
			TestEquality(TEXT("empty different capacities"), true, TRingBuffer<int32>(0), TRingBuffer<int32>(8));
			TestEquality(TEXT("equal nonempty powerof2"), true, TRingBuffer<int32>({ 0, 1, 2, 3 }), TRingBuffer<int32>({ 0, 1, 2, 3 }));
			TestEquality(TEXT("equal nonempty nonpowerof2"), true, TRingBuffer<int32>({ 0, 1, 2, 3, 4, 5 }), TRingBuffer<int32>({ 0, 1, 2, 3, 4, 5 }));
			{
				TRingBuffer<int32> QNum6Cap16(16);
				for (int32 Index = 0; Index < 6; ++Index)
				{
					QNum6Cap16.PushBack(Index);
				}
				TestEquality(TEXT("equal nonempty different capacities"), true, QNum6Cap16, TRingBuffer<int32>({ 0, 1, 2, 3, 4, 5 }));
			}

			TestEquality(TEXT("empty to nonempty"), false, TRingBuffer<int32>(0), TRingBuffer<int32>({ 0, 1, 2, 3, 4, 5 }));
			TestEquality(TEXT("smaller size to bigger size"), false, TRingBuffer<int32>({ 0, 1, 2 }), TRingBuffer<int32>({ 0, 1, 2, 3, 4, 5 }));
			TestEquality(TEXT("same size different elements"), false, TRingBuffer<int32>({ 0, 1, 2 }), TRingBuffer<int32>({ 0, 1, 3 }));
			TestEquality(TEXT("same elements different order"), false, TRingBuffer<int32>({ 0, 1, 2 }), TRingBuffer<int32>({ 0, 2, 1 }));

			for (int HasPow2 = 0; HasPow2 < 2; ++HasPow2)
			{
				const int Count = HasPow2 ? 8 : 7;
				TRingBuffer<int32> Q0Pop;
				TRingBuffer<int32> Q1PopFront;
				TRingBuffer<int32> Q2PopFront;
				TRingBuffer<int32> Q1PopBack;
				TRingBuffer<int32> Q2PopBack;
				TRingBuffer<int32> Q2PopFront3PopBack;
				Q1PopFront.PushBack(47);
				Q2PopFront.PushBack(576);
				Q2PopFront.PushBack(-5);
				Q2PopFront3PopBack.PushBack(84);
				Q2PopFront3PopBack.PushBack(1000);
				for (int Index = 0; Index < Count; ++Index)
				{
					Q0Pop.PushBack(Index);
					Q1PopFront.PushBack(Index);
					Q2PopFront.PushBack(Index);
					Q1PopBack.PushBack(Index);
					Q2PopBack.PushBack(Index);
					Q2PopFront3PopBack.PushBack(Index);
				}
				Q1PopFront.PopFront();
				Q2PopFront.PopFront();
				Q2PopFront.PopFront();
				Q1PopBack.PushBack(-18);
				Q1PopBack.PopBack();
				Q2PopBack.PushBack(105);
				Q2PopBack.PushBack(219);
				Q2PopBack.PopBack();
				Q2PopBack.PopBack();
				Q2PopFront3PopBack.PushBack(456);
				Q2PopFront3PopBack.PushBack(654);
				Q2PopFront3PopBack.PushBack(8888888);
				Q2PopFront3PopBack.PopFront();
				Q2PopFront3PopBack.PopBack();
				Q2PopFront3PopBack.PopFront();
				Q2PopFront3PopBack.PopBack();
				Q2PopFront3PopBack.PopBack();

				const TCHAR* Names[] =
				{
					TEXT("Q0Pop"),
					TEXT("Q1PopFront"),
					TEXT("Q2PopFront"),
					TEXT("Q1PopBack"),
					TEXT("Q2PopBack"),
					TEXT("Q2PopFront3PopBack"),
				};
				TRingBuffer<int32>* Pops[] =
				{
					&Q0Pop,
					&Q1PopFront,
					&Q2PopFront,
					&Q1PopBack,
					&Q2PopBack,
					&Q2PopFront3PopBack
				};


				auto TestThesePops = [this, HasPow2, &TestEquality, &Names, &Pops](int TrialA, int TrialB)
				{
					TestEquality(*FString::Printf(TEXT("%s - %s - %s"), Names[TrialA], Names[TrialB], (HasPow2 ? TEXT("powerof2") : TEXT("nonpowerof2"))), true, *Pops[TrialA], *Pops[TrialB]);
				};

				for (int TrialA = 0; TrialA < UE_ARRAY_COUNT(Names); ++TrialA)
				{
					for (int TrialB = TrialA /* test each against itself as well */; TrialB < UE_ARRAY_COUNT(Names); ++TrialB)
					{
						TestThesePops(TrialA, TrialB);
					}
				}
			}
		}

		// Test Push and pop all
		for (int Direction = 0; Direction < 2; ++Direction)
		{
			bool bIsPushBack = Direction == 0;
			auto GetMessage = [&bIsPushBack](const TCHAR* Message)
			{
				return FString::Printf(TEXT("Test %s (%s)"), Message, (bIsPushBack ? TEXT("PushBack") : TEXT("PushFront")));
			};

			// Test Mixed Pushes and Pops
			{
				const TRingBuffer<uint32>::IndexType ElementsToPush = 256;
				const TRingBuffer<uint32>::IndexType ElementPopMod = 16;
				const TRingBuffer<uint32>::IndexType ExpectedSize = 256 - ElementPopMod;
				const TRingBuffer<uint32>::IndexType ExpectedCapacity = 256;

				TRingBuffer<uint32> Q(4);

				uint32 ExpectedPoppedValue = 0;
				for (uint32 It = 0; It < 256; ++It)
				{
					if (bIsPushBack)
					{
						Q.PushBack(It);
						TestEqual(*GetMessage(TEXT("Push and pop - Push")), It, Q[Q.Num() - 1]);
					}
					else
					{
						Q.PushFront(It);
						TestEqual(*GetMessage(TEXT("Push and pop - Push")), It, Q[0]);
					}

					if (It % ElementPopMod == 0)
					{
						uint32 PoppedValue;
						if (bIsPushBack)
						{
							PoppedValue = Q[0];
							Q.PopFront();
						}
						else
						{
							PoppedValue = Q[Q.Num() - 1];
							Q.PopBack();
						}
						TestEqual(*GetMessage(TEXT("Push and pop - Pop")), ExpectedPoppedValue, PoppedValue);
						++ExpectedPoppedValue;
					}
				}

				TestEqual(*GetMessage(TEXT("Push and pop - Size")), Q.Num(), ExpectedSize);
				TestEqual(*GetMessage(TEXT("Push and pop - Capacity")), Q.GetCapacity(), ExpectedCapacity);
				TestTrue(*GetMessage(TEXT("Push and pop - IntegerRange")), IsIntegerRange(Q, ExpectedPoppedValue, ExpectedPoppedValue + ExpectedSize, bIsPushBack));
			}


			// Popping down to empty
			{
				const TRingBuffer<uint32>::IndexType ElementsToPush = 256;

				TRingBuffer<uint32> Q(ElementsToPush);

				TestTrue(*GetMessage(TEXT("Push and pop all - IsEmpty before")), Q.IsEmpty());
				TestEqual(*GetMessage(TEXT("Push and pop all - Size before")), Q.Num(), 0);

				for (TRingBuffer<int32>::IndexType It = 0; It < ElementsToPush; ++It)
				{
					if (bIsPushBack)
					{
						Q.PushBack(It);
					}
					else
					{
						Q.PushFront(It);
					}
				}

				TestEqual(*GetMessage(TEXT("Push and pop all - Size")), Q.Num(), ElementsToPush);
				TestEqual(*GetMessage(TEXT("Push and pop all - Capacity")), Q.GetCapacity(), ElementsToPush);
				TestTrue(*GetMessage(TEXT("Push and pop all - Expected")), IsIntegerRange(Q, 0, ElementsToPush, bIsPushBack));

				for (TRingBuffer<int32>::IndexType It = 0; It < ElementsToPush; ++It)
				{
					if (bIsPushBack)
					{
						Q.PopFront();
					}
					else
					{
						Q.PopBack();
					}
				}

				TestTrue(*GetMessage(TEXT("Push and pop all - IsEmpty after")), Q.IsEmpty());
				TestEqual(*GetMessage(TEXT("Push and pop all - Size after")), Q.Num(), 0);
				TestEqual(*GetMessage(TEXT("Push and pop all - Capacity after")), Q.GetCapacity(), ElementsToPush);
			}

			// Test index wrap
			{
				for (int32 Offset : {-12, -8, -5, -1, 0, 2, 7, 8, 15})
				{
					const TRingBuffer<uint32>::IndexType ElementsToPush = 256;
					const TRingBuffer<uint32>::IndexType ElementPopMod = 16;
					const TRingBuffer<uint32>::IndexType ExpectedSize = 256 - ElementPopMod;
					const TRingBuffer<uint32>::IndexType ExpectedCapacity = 256;

					TRingBuffer<uint32> Q(8);

					// Set front and afterback to an arbitrary offset
					// Note that AfterBack is always exactly equal to Front + Num()
					Q.Front = Offset;
					Q.AfterBack = Q.Front;

					TestTrue(*GetMessage(TEXT("index wrap - IsEmpty before")), Q.IsEmpty());
					TestEqual(*GetMessage(TEXT("index wrap - Size before")), Q.Num(), 0);

					for (TRingBuffer<uint32>::IndexType It = 0; It < ElementsToPush; ++It)
					{
						if (bIsPushBack)
						{
							Q.PushBack(It);
						}
						else
						{
							Q.PushFront(It);
						}
					}

					TestEqual(*GetMessage(TEXT("index wrap - Size")), Q.Num(), ElementsToPush);
					TestEqual(*GetMessage(TEXT("index wrap - Capacity")), Q.GetCapacity(), ElementsToPush);
					TestTrue(*GetMessage(TEXT("index wrap - Expected")), IsIntegerRange(Q, 0, ElementsToPush, bIsPushBack));

					for (TRingBuffer<int32>::IndexType It = 0; It < ElementsToPush; ++It)
					{
						if (bIsPushBack)
						{
							Q.PopFront();
						}
						else
						{
							Q.PopBack();
						}
					}

					TestTrue(*GetMessage(TEXT("index wrap - IsEmpty after")), Q.IsEmpty());
					TestEqual(*GetMessage(TEXT("index wrap - Size after")), Q.Num(), 0);
					TestEqual(*GetMessage(TEXT("index wrap - Capacity after")), Q.GetCapacity(), ElementsToPush);
				}
			}
		}

		// Test Trim
		{
			const TRingBuffer<int32>::IndexType ElementsToPush = 9;
			const TRingBuffer<int32>::IndexType ElementsToPop = 5;
			const TRingBuffer<int32>::IndexType ExpectedCapacity = 16;
			const TRingBuffer<int32>::IndexType ExpectedCapacityAfterTrim = 4;

			TRingBuffer<uint32> Q(0);

			for (TRingBuffer<int32>::IndexType It = 0; It < ElementsToPush; ++It)
			{
				Q.PushBack(It);
			}

			TestEqual(TEXT("Test Trim - Size"), Q.Num(), ElementsToPush);
			TestEqual(TEXT("Test Trim - Capacity"), Q.GetCapacity(), ExpectedCapacity);
			TestTrue(TEXT("Test Trim - Expected"), IsIntegerRange(Q, 0, ElementsToPush));

			for (TRingBuffer<int32>::IndexType It = 0; It < ElementsToPop; ++It)
			{
				Q.PopFront();
			}

			Q.Trim();

			TestEqual(TEXT("Test Trim - Size"), Q.Num(), ElementsToPush - ElementsToPop);
			TestEqual(TEXT("Test Trim - Capacity"), Q.GetCapacity(), ExpectedCapacityAfterTrim);
			TestTrue(TEXT("Test Trim - Expected"), IsIntegerRange(Q, ElementsToPop, ElementsToPush));
		}

		// Test Front and Back acting as two stacks
		{
			TRingBuffer<uint32> Q;

			const uint32 ElementsToPush = 64;
			const uint32 ElementPopMod = 5;

			for (uint32 It = 0; It < ElementsToPush; ++It)
			{
				Q.PushBack(It);
				TestEqual(TEXT("Test TwoStacks - PushBack"), Q.GetBack(), It);
				Q.PushFront(It);
				TestEqual(TEXT("Test TwoStacks - PushFront"), Q.GetFront(), It);
				if (It % ElementPopMod == 0)
				{
					uint32 PushValue = 0xfefefefe;
					Q.PushBack(PushValue);
					TestEqual(TEXT("Test TwoStacks - Sporadic PopBack"), Q.GetBack(), PushValue);
					Q.PopBack();
					Q.PushFront(PushValue);
					TestEqual(TEXT("Test TwoStacks - Sporadic PopFront"), Q.GetFront(), PushValue);
					Q.PopFront();
				}
			}

			TestEqual(TEXT("Test TwoStacks - MiddleSize"), Q.Num(), ElementsToPush * 2);
			for (uint32 It = 0; It < ElementsToPush * 2; ++It)
			{
				TestEqual(*FString::Printf(TEXT("TwoStacks - Middle value %d"), It), Q[It], (It < ElementsToPush ? ElementsToPush - 1 - It : It - ElementsToPush));
			}

			for (uint32 It = 0; It < ElementsToPush; ++It)
			{
				TestEqual(TEXT("Test TwoStacks - Final PopBack"), Q.GetBack(), ElementsToPush - 1 - It);
				Q.PopBack();
				TestEqual(TEXT("Test TwoStacks - Final PopFront"), Q.GetFront(), ElementsToPush - 1 - It);
				Q.PopFront();
			}

			TestEqual(TEXT("Test TwoStacks - FinalSize"), Q.Num(), 0);
		}

		// Test pushing into space that has been cleared from popping on the other side
		{
			for (int Direction = 0; Direction < 2; ++Direction)
			{
				bool bIsPushBack = Direction == 0;
				auto GetMessage = [bIsPushBack](const TCHAR* Message)
				{
					return FString::Printf(TEXT("Test PushIntoPop - %s (%s)"), Message, (bIsPushBack ? TEXT("PushBack") : TEXT("PushFront")));
				};
				TRingBuffer<uint32> Q({ 0,1,2,3,4,5,6,7 });
				TRingBuffer<int32>::IndexType InitialSize = 8;
				TestEqual(*GetMessage(TEXT("InitialSize")), InitialSize, Q.Num());
				TestEqual(*GetMessage(TEXT("InitialCapacity (Implementation Detail)")), InitialSize, Q.GetCapacity());

				if (bIsPushBack)
				{
					Q.PopBack();
				}
				else
				{
					Q.PopFront();
				}
				TestEqual(*GetMessage(TEXT("PoppedSize")), InitialSize - 1, Q.Num());
				TestEqual(*GetMessage(TEXT("PoppedCapacity")), InitialSize, Q.GetCapacity());

				if (bIsPushBack)
				{
					Q.PushFront(8);
				}
				else
				{
					Q.PushBack(8);
				}
				TestEqual(*GetMessage(TEXT("PushedSize")), InitialSize, Q.Num());
				TestEqual(*GetMessage(TEXT("PushedCapacity")), InitialSize, Q.GetCapacity());
				if (bIsPushBack)
				{
					TestEqual(*GetMessage(TEXT("PushedValues")), Q, TRingBuffer<uint32>({ 8,0,1,2,3,4,5,6 }));
				}
				else
				{
					TestEqual(*GetMessage(TEXT("PushedValues")), Q, TRingBuffer<uint32>({ 1,2,3,4,5,6,7,8 }));
				}

				if (bIsPushBack)
				{
					Q.PushFront(9);
				}
				else
				{
					Q.PushBack(9);
				}
				TestEqual(*GetMessage(TEXT("Second PushedSize")), InitialSize + 1, Q.Num());
				TestEqual(*GetMessage(TEXT("Second PushedCapacity")), static_cast<uint32>(FMath::RoundUpToPowerOfTwo(InitialSize + 1)), Q.GetCapacity());
				if (bIsPushBack)
				{
					TestEqual(*GetMessage(TEXT("Second PushedValues")), Q, TRingBuffer<uint32>({ 9,8,0,1,2,3,4,5,6 }));
				}
				else
				{
					TestEqual(*GetMessage(TEXT("Second PushedValues")), Q, TRingBuffer<uint32>({ 1,2,3,4,5,6,7,8,9 }));
				}
			}
		}

		// Test Empty to a capacity
		{
			TRingBuffer<uint32> Q(16);
			TestEqual(TEXT("Test EmptyToCapacity - InitialCapacity"), 16, Q.GetCapacity());
			Q.Empty(8);
			TestEqual(TEXT("Test EmptyToCapacity - Lower"), 8, Q.GetCapacity());
			Q.Empty(32);
			TestEqual(TEXT("Test EmptyToCapacity - Higher"), 32, Q.GetCapacity());
		}

		// Test Different Push constructors
		{
			auto Clear = []()
			{
				Counter::Clear();
			};
			auto TestCounts = [this](const TCHAR* Message, int32 NumVoid, int32 NumCopy, int32 NumMove, int32 NumDestruct)
			{
				TestTrue(Message, NumVoid == Counter::NumVoid && NumCopy == Counter::NumCopy && NumMove == Counter::NumMove && NumDestruct == Counter::NumDestruct);
			};

			Clear();
			{
				TRingBuffer<Counter> QEmpty(4);
				QEmpty.Reserve(8);
				QEmpty.Empty();
				TRingBuffer<Counter> QEmpty2(4);
			}
			TestCounts(TEXT("Test Push Constructors - Unallocated elements call no constructors/destructors"), 0, 0, 0, 0);
			{
				TRingBuffer<Counter> QEmpty(4);
				QEmpty.EmplaceBack();
				QEmpty.PopBack();
				Clear();
			}
			TestCounts(TEXT("Test Push Constructors - Already removed element calls no destructors"), 0, 0, 0, 0);


			uint32 MarkerValue = 0x54321;
			Counter CounterA(MarkerValue);

			TRingBuffer<Counter> Q(4);
			Clear();
			for (int Direction = 0; Direction < 2; ++Direction)
			{
				bool bPushBack = Direction == 0;
				auto TestDirCounts = [this, bPushBack, &TestCounts, &Q, &Clear, MarkerValue](const TCHAR* Message, int32 NumVoid, int32 NumCopy, int32 NumMove, int32 NumDestruct, bool bWasInitialized = true)
				{
					const TCHAR* DirectionText = bPushBack ? TEXT("Back") : TEXT("Front");
					bool bElementExists = Q.Num() == 1;
					TestTrue(*FString::Printf(TEXT("Test Push Constructors - %s%s ElementExists"), Message, DirectionText), bElementExists);
					if (bWasInitialized && bElementExists)
					{
						TestTrue(*FString::Printf(TEXT("Test Push Constructors - %s%s ValueEquals"), Message, DirectionText), Q.GetFront().Value == MarkerValue);
					}
					Q.PopFront();
					TestCounts(*FString::Printf(TEXT("Test Push Constructors - %s%s CountsEqual"), Message, DirectionText), NumVoid, NumCopy, NumMove, NumDestruct);
					Clear();
				};

				if (bPushBack) Q.PushBack(CounterA); else Q.PushFront(CounterA);
				TestDirCounts(TEXT("Copy Push"), 0, 1, 0, 1);
				if (bPushBack) Q.PushBack_GetRef(CounterA); else Q.PushFront_GetRef(CounterA);
				TestDirCounts(TEXT("Copy GetRef Push"), 0, 1, 0, 1);
				if (bPushBack) Q.PushBack(MoveTemp(CounterA)); else Q.PushFront(MoveTemp(CounterA));
				TestDirCounts(TEXT("Move Push"), 0, 0, 1, 1);
				if (bPushBack) Q.PushBack_GetRef(MoveTemp(CounterA)); else Q.PushFront_GetRef(MoveTemp(CounterA));
				TestDirCounts(TEXT("Move GetRef Push"), 0, 0, 1, 1);
				if (bPushBack) Q.EmplaceBack(MarkerValue); else Q.EmplaceFront(MarkerValue);
				TestDirCounts(TEXT("Emplace"), 1, 0, 0, 1);
				if (bPushBack) Q.EmplaceBack_GetRef(MarkerValue); else Q.EmplaceFront_GetRef(MarkerValue);
				TestDirCounts(TEXT("GetRef Emplace"), 1, 0, 0, 1);
				if (bPushBack) Q.PushBackUninitialized(); else Q.PushFrontUninitialized();
				TestDirCounts(TEXT("Uninitialized Push"), 0, 0, 0, 1, false);
				if (bPushBack) Q.PushBackUninitialized_GetRef(); else Q.PushFrontUninitialized_GetRef();
				TestDirCounts(TEXT("Uninitialized GetRef Push"), 0, 0, 0, 1, false);
			}
		}

		TestShiftIndex<uint32>();
		TestShiftIndex<Counter>();

		// Test RemoveAt
		{
			{
				TRingBuffer<uint32> Q{ 0,1,2,3,4,5,6,7 };
				Q.RemoveAt(2);
				TestEqual(TEXT("Test RemoveAt Front Closest"), TRingBuffer<uint32>({ 0,1,3,4,5,6,7 }), Q);
			}
			{
				TRingBuffer<uint32> Q{ 0,1,2,3,4,5,6,7 };
				Q.RemoveAt(5);
				TestEqual(TEXT("Test RemoveAt Back Closest"), TRingBuffer<uint32>({ 0,1,2,3,4,6,7 }), Q);
			}
			{
				TRingBuffer<uint32> Q{ 0,1,2,3,4,5,6,7 };
				int32 Offset = 4;
				Q.Front += Offset;
				Q.AfterBack += Offset;
				//Now equal to: TRingBuffer<uint32> Q{ 4,5,6,7,0,1,2,3 };
				Q.RemoveAt(2);
				TestEqual(TEXT("Test RemoveAt Front Closest With Offset"), TRingBuffer<uint32>({ 4,5,7,0,1,2,3 }), Q);
			}
			{
				TRingBuffer<uint32> Q{ 0,1,2,3,4,5,6,7 };
				int32 Offset = 4;
				Q.Front += Offset;
				Q.AfterBack += Offset;
				//Now equal to: TRingBuffer<uint32> Q{ 4,5,6,7,0,1,2,3 };
				Q.RemoveAt(5);
				TestEqual(TEXT("Test RemoveAt Back Closest With Offset"), TRingBuffer<uint32>({ 4,5,6,7,0,2,3 }), Q);
			}
			{
				TRingBuffer<uint32> Q{ 0,1 };
				Q.RemoveAt(-1);
				Q.RemoveAt(2);
				TestEqual(TEXT("Test RemoveAt OutOfRange"), Q.Num(), 2);
				TRingBuffer<uint32> QEmpty;
				QEmpty.RemoveAt(-1);
				QEmpty.RemoveAt(0);
				QEmpty.RemoveAt(1);
				TestEqual(TEXT("Test RemoveAt OutOfRange Empty"), QEmpty.Num(), 0);
			}
		}

		// Test Iteration
		{
			{
				TRingBuffer<uint32> Q{ 0,1,2,3,4,5,6,7 };
				uint32 Counter = 0;
				for (uint32 Value : Q)
				{
					TestEqual(TEXT("Test Iteration - Value"), Counter++, Value);
				}
				TestEqual(TEXT("Test Iteration - Num"), Counter, 8);
			}
			{
				TRingBuffer<uint32> Q{ 4,5,6,7,0,1,2,3 };
				int32 Offset = 4;
				Q.Front += Offset;
				Q.AfterBack += Offset;
				// Now equal to 0,1,2,3,4,5,6,7
				uint32 Counter = 0;
				for (uint32 Value : Q)
				{
					TestEqual(TEXT("Test Iteration with Offset - Value"), Counter++, Value);
				}
				TestEqual(TEXT("Test Iteration with Offset  - Num"), Counter, 8);
			}
		}

		// Test ConvertReferenceToIndex
		{
			{
				TRingBuffer<uint32> Q{ 4,5,6,7,0,1,2,3 };
				int32 Offset = 4;
				Q.Front += Offset;
				Q.AfterBack += Offset;
				// Now equal to 0,1,2,3,4,5,6,7
				TestEqual(TEXT("Test ConvertReferenceToIndex - before array"), Q.ConvertReferenceToIndex(*(&Q[0] - 100)), INDEX_NONE);
				TestEqual(TEXT("Test ConvertReferenceToIndex - after array"), Q.ConvertReferenceToIndex(*(&Q[0] + 100)), INDEX_NONE);
				for (int32 It = 0; It < 8; ++It)
				{
					TestEqual(TEXT("Test ConvertReferenceToIndex - Values"), Q.ConvertReferenceToIndex(Q[It]), It);
				}
			}

			{
				TRingBuffer<uint32> Q(16);
				for (int32 It = 7; It >= 0; --It)
				{
					Q.PushFront(It);
				}
				Q.PopBack();
				// 8 Invalids, followed by 0,1,2,3,4,5,6, followed by Invalid
				for (int32 It = 0; It < 7; ++It)
				{
					TestEqual(TEXT("Test ConvertReferenceToIndex - Cap - Values"), Q.ConvertReferenceToIndex(Q[It]), It);
				}
				TestEqual(TEXT("Test ConvertReferenceToIndex - Cap - After End"), Q.ConvertReferenceToIndex(*(&Q[6] + 1)), INDEX_NONE);
				TestEqual(TEXT("Test ConvertReferenceToIndex - Cap - Before Start"), Q.ConvertReferenceToIndex(*(&Q[0] - 1)), INDEX_NONE);
			}
		}

		// Test that setting Front to its maximum value and then popping the maximum number of elements does not break the contract that Front < capacity in StorageModulo space
		{
			TRingBuffer<uint32> Q(8);
			Q.PushFront(0);
			for (uint32 It = 1; It < 8; ++It)
			{
				Q.PushBack(It);
			}
			TestTrue(TEXT("Test Front<Capacity - Setup"), (Q.Front & Q.IndexMask) == Q.IndexMask && Q.Num() == Q.GetCapacity());
			Q.PopFront(8);
			TestTrue(TEXT("Test Front<Capacity - Contract is true"), static_cast<uint32>(Q.Front) < static_cast<uint32>(Q.GetCapacity()));
		}

		// Test IsValidIndex
		{
			TRingBuffer<uint32> Q({ 0,1,2,3,4 });
			for (int32 It = 0; It < Q.Num(); ++It)
			{
				TestEqual(TEXT("IsValidIndex - InRange"), Q.IsValidIndex(It), true);
			}
			TestEqual(TEXT("IsValidIndex - Negative"), Q.IsValidIndex(-1), false);
			TestEqual(TEXT("IsValidIndex - Num()"), Q.IsValidIndex(Q.Num() + 1), false);
			TestEqual(TEXT("IsValidIndex - Capacity"), Q.IsValidIndex(Q.GetCapacity()), false);
			TestEqual(TEXT("IsValidIndex - Capacity + 1"), Q.IsValidIndex(Q.GetCapacity()+1), false);
		}

		// Test MakeContiguous
		{
			{
				TRingBuffer<uint32> QEmpty;
				TestEqual(TEXT("MakeContiguous - Empty zero capacity"), QEmpty.MakeContiguous().Num(), 0);
				QEmpty.PushBack(1);
				QEmpty.PopFront();
				TestEqual(TEXT("MakeContiguous - Empty non-zero capacity"), QEmpty.MakeContiguous().Num(), 0);
			}
			{
				TArrayView<uint32> View;
				TRingBuffer<uint32> Q(8);
				Q.PushFront(37);
				View = Q.MakeContiguous();
				TestTrue(TEXT("MakeContiguous - Front at end"), ArrayViewsEqual(View, TArrayView<const uint32>({ 37 })));
			}
			{
				TArrayView<uint32> View;
				TRingBuffer<uint32> Q(8);
				for (TRingBuffer<uint32>::IndexType It = 0; It < 6; ++It)
				{
					Q.PushBack(It);
				}
				Q.PopFront();
				TRingBuffer<uint32>::StorageModuloType SavedFront = Q.Front;
				TestTrue(TEXT("MakeContiguous - Front in middle - setup"), SavedFront > 0);
				View = Q.MakeContiguous();
				TestTrue(TEXT("MakeContiguous - Front in middle - values"), ArrayViewsEqual(View, TArrayView<const uint32>({ 1,2,3,4,5 })));
				TestTrue(TEXT("MakeContiguous - Front in middle - no reallocate"), Q.Front == SavedFront);
			}
			{
				TArrayView<uint32> View;
				TRingBuffer<uint32> Q(8);
				for (TRingBuffer<uint32>::IndexType It = 1; It < 8; ++It)
				{
					Q.PushBack(It);
				}
				Q.PushFront(0);
				TestTrue(TEXT("MakeContiguous - Full array front at end - setup"), (Q.Front & Q.IndexMask) == 7);
				View = Q.MakeContiguous();
				TestTrue(TEXT("MakeContiguous - Full array front at end - values"), ArrayViewsEqual(View, TArrayView<const uint32>({0,1,2,3,4,5,6,7 })));
				TestTrue(TEXT("MakeContiguous - Full array front at end - reallocated"), Q.Front == 0);
			}
			{
				TArrayView<uint32> View;
				TRingBuffer<uint32> Q(8);
				for (TRingBuffer<uint32>::IndexType It = 0; It < 8; ++It)
				{
					Q.PushBack(It);
				}
				uint32* SavedData = Q.AllocationData;
				TestTrue(TEXT("MakeContiguous - Full array front at start - setup"), Q.Front == 0);
				View = Q.MakeContiguous();
				TestTrue(TEXT("MakeContiguous - Full array front at start - values"), ArrayViewsEqual(View, TArrayView<const uint32>({ 0,1,2,3,4,5,6,7 })));
				TestTrue(TEXT("MakeContiguous - Full array front at start - no reallocate"), Q.AllocationData == SavedData);
			}
		}

		// Test Remove
		{
			Counter Value;
			{
				TRingBuffer<Counter> Q;
				Value.Value = 2;
				Counter::Clear();
				TestEqual(TEXT("Remove - empty"), Q.Remove(Value), 0);
				TestEqual(TEXT("Remove - empty - destructor count"), Counter::NumDestruct, 0);
			}
			{
				TRingBuffer<Counter> Q({ 0,1,2,3,4 });
				Value.Value = 5;
				Counter::Clear();
				TestEqual(TEXT("Remove - no hits"), Q.Remove(Value), 0);
				TestEqual(TEXT("Remove - no hits - destructor count"), Counter::NumDestruct, 0);
				Q.PushBack(5);
				TestTrue(TEXT("Remove - no hits - values"), Q == TRingBuffer<Counter>({ 0,1,2,3,4,5 }));
			}
			{
				TRingBuffer<Counter> Q({ 1,2,3,4 });
				Q.PushFront(0);
				Value.Value = 0;
				Counter::Clear();
				TestEqual(TEXT("Remove - one element at front - num"), Q.Remove(Value), 1);
				TestEqual(TEXT("Remove - one element at front - destructor count"), Counter::NumDestruct, 5);
				Q.PushBack(5);
				TestTrue(TEXT("Remove - one element at front - values"), Q == TRingBuffer<Counter>({ 1,2,3,4,5 }));
			}
			{
				TRingBuffer<Counter> Q({ 0,1,2,3,4 });
				Value.Value = 2;
				Counter::Clear();
				TestEqual(TEXT("Remove - one element in mid - num"), Q.Remove(Value), 1);
				TestEqual(TEXT("Remove - one element in mid - destructor count"), Counter::NumDestruct, 3);
				Q.PushBack(5);
				TestTrue(TEXT("Remove - one element in mid - values"), Q == TRingBuffer<Counter>({ 0,1,3,4,5 }));
			}
			{
				TRingBuffer<Counter> Q({ 1,2,3,4 });
				Q.PushFront(0);
				Value.Value = 2;
				Counter::Clear();
				TestEqual(TEXT("Remove - one element in mid - front at end"), Q.Remove(Value), 1);
				TestEqual(TEXT("Remove - one element in mid - front at end - destructor count"), Counter::NumDestruct, 3);
				Q.PushBack(5);
				TestTrue(TEXT("Remove - one element in mid - front at end - values"), Q == TRingBuffer<Counter>({ 0,1,3,4,5 }));
			}
			{
				TRingBuffer<Counter> Q({ 0,1,2,3,4 });
				Value.Value = 4;
				Counter::Clear();
				TestEqual(TEXT("Remove - one element - element at end - num"), Q.Remove(Value), 1);
				TestEqual(TEXT("Remove - one element - element at end - destructor count"), Counter::NumDestruct, 1);
				Q.PushBack(5);
				TestTrue(TEXT("Remove - one element - element at end - values"), Q == TRingBuffer<Counter>({ 0,1,2,3,5 }));
			}
			{
				TRingBuffer<Counter> Q({ 1,2,3,4 });
				Q.PushFront(4);
				Value.Value = 4;
				Counter::Clear();
				TestEqual(TEXT("Remove - one element at front one at end - num"), Q.Remove(Value), 2);
				TestEqual(TEXT("Remove - one element at front one at end - destructor count"), Counter::NumDestruct, 5);
				Q.PushBack(5);
				TestTrue(TEXT("Remove - one element at front one at end - values"), Q == TRingBuffer<Counter>({ 1,2,3,5 }));
			}
			{
				TRingBuffer<Counter> Q({ 1,2,3,4 });
				Q.PushFront(1);
				Value.Value = 1;
				Counter::Clear();
				TestEqual(TEXT("Remove - two elements - front at end - num"), Q.Remove(Value), 2);
				TestEqual(TEXT("Remove - two elements - front at end - destructor count"), Counter::NumDestruct, 5);
				Q.PushBack(5);
				TestTrue(TEXT("Remove - two elements - front at end - values"), Q == TRingBuffer<Counter>({ 2,3,4,5 }));
			}
		}

		return true;
	}

	template <typename T>
	void TestShiftIndex()
	{
		// Test shifts at specific points
		{
			{
				TRingBuffer<T> Q{ 0,1,2,3,4,5,6,7 };
				Q.ShiftIndexToFront(5);
				TestEqual(TEXT("ShiftIndexToFront"), TRingBuffer<T>({ 5,0,1,2,3,4,6,7 }), Q);
				Q.ShiftIndexToBack(3);
				TestEqual(TEXT("ShiftIndexToBack"), TRingBuffer<T>({ 5,0,1,3,4,6,7,2 }), Q);
			}

			{
				TRingBuffer<T> Q{ 0,1,2,3,4,5,6,7 };
				int32 Offset = 4;
				Q.Front += Offset;
				Q.AfterBack += Offset;
				//Now equal to: TRingBuffer<uint32> Q{ 4,5,6,7,0,1,2,3 };
				Q.ShiftIndexToFront(5);
				TestEqual(TEXT("ShiftIndexToFront With Offset"), TRingBuffer<T>({ 1,4,5,6,7,0,2,3 }), Q);
				Q.ShiftIndexToBack(3);
				TestEqual(TEXT("ShiftIndexToBack With Offset"), TRingBuffer<T>({ 1,4,5,7,0,2,3,6 }), Q);
			}

			{
				TRingBuffer<T> Q{ 0,1,2,3,4,5,6,7,8 };
				TestEqual(TEXT("ShiftIndexToFront Cap - Capacity"), Q.GetCapacity(), 16);
				Q.ShiftIndexToFront(5);
				TestEqual(TEXT("ShiftIndexToFront Cap"), TRingBuffer<T>({ 5,0,1,2,3,4,6,7,8 }), Q);
				Q.ShiftIndexToBack(3);
				TestEqual(TEXT("ShiftIndexToBack Cap"), TRingBuffer<T>({ 5,0,1,3,4,6,7,8,2 }), Q);
			}

			{
				TRingBuffer<T> Q(16);
				for (int32 It = 7; It >= 0; --It)
				{
					Q.PushFront(It);
				}
				Q.PopBack();
				// 8 Invalids, followed by 0,1,2,3,4,5,6, followed by Invalid
				Q.ShiftIndexToFront(5);
				TestEqual(TEXT("ShiftIndexToFront Cap With Offset"), TRingBuffer<T>({ 5,0,1,2,3,4,6 }), Q);
				Q.ShiftIndexToBack(3);
				TestEqual(TEXT("ShiftIndexToBack Cap With Offset"), TRingBuffer<T>({ 5,0,1,3,4,6,2 }), Q);
			}

			{
				TRingBuffer<T> Q(16);
				for (int32 It = 7; It >= 0; --It)
				{
					Q.PushFront(It);
				}
				Q.PushBack(8);
				// 8, (AfterBack), followed by 7 Invalids, followed by (Start) 0,1,2,3,4,5,6,7
				Q.ShiftIndexToFront(8);
				TestEqual(TEXT("ShiftIndexToFront Cap With Wrapped"), TRingBuffer<T>({ 8,0,1,2,3,4,5,6,7 }), Q);
				Q.ShiftIndexToBack(0);
				TestEqual(TEXT("ShiftIndexToBack Cap With Wrapped"), TRingBuffer<T>({ 0,1,2,3,4,5,6,7,8 }), Q);
			}
		}

		// Test ShiftIndex of each possible index
		{
			int32 Count = 8;
			for (int32 It = 0; It < Count; ++It)
			{
				TRingBuffer<T> Q({ 0,1,2,3,4,5,6,7 });
				Q.ShiftIndexToBack(It);
				int32 CheckIndex = 0;
				for (; CheckIndex < It; ++CheckIndex)
				{
					TestEqual(*FString::Printf(TEXT("ShiftIndexToBack Entire Array Values (%d,%d)"), It, CheckIndex), CheckIndex, Q[CheckIndex]);
				}
				for (; CheckIndex < Count - 1; ++CheckIndex)
				{
					TestEqual(*FString::Printf(TEXT("ShiftIndexToBack Entire Array Values (%d,%d)"), It, CheckIndex), CheckIndex + 1, Q[CheckIndex]);
				}
				TestEqual(*FString::Printf(TEXT("ShiftIndexToBack Entire Array Values (%d,%d)"), It, Count - 1), It, Q[Count - 1]);
			}
			for (int32 It = 0; It < Count; ++It)
			{
				TRingBuffer<T> Q({ 0,1,2,3,4,5,6,7 });
				Q.ShiftIndexToFront(It);

				TestEqual(*FString::Printf(TEXT("ShiftIndexToFront Entire Array Values (%d,%d)"), It, 0), It, Q[0]);
				int32 CheckIndex = 1;
				for (; CheckIndex <= It; ++CheckIndex)
				{
					TestEqual(*FString::Printf(TEXT("ShiftIndexToFront Entire Array Values (%d,%d)"), It, CheckIndex), CheckIndex - 1, Q[CheckIndex]);
				}
				for (; CheckIndex < Count; ++CheckIndex)
				{
					TestEqual(*FString::Printf(TEXT("ShiftIndexToFront Entire Array Values (%d,%d)"), It, CheckIndex), CheckIndex, Q[CheckIndex]);
				}
			}
		}
	}

	template <typename T, typename U>
	static bool ArrayViewsEqual(const TArrayView<T>& A, const TArrayView<U>& B)
	{
		int32 Num = A.Num();
		if (Num != B.Num())
		{
			return false;
		}
		for (int It = 0; It < Num; ++It)
		{
			if (A[It] != B[It])
			{
				return false;
			}
		}
		return true;
	}
};

int FRingBufferTest::Counter::NumVoid = 0;
int FRingBufferTest::Counter::NumCopy = 0;
int FRingBufferTest::Counter::NumMove = 0;
int FRingBufferTest::Counter::NumDestruct = 0;

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FRingBufferTestSubClass, FRingBufferTest, "System.Core.Containers.RingBuffer", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter);
bool FRingBufferTestSubClass::RunTest(const FString& Parameters)
{
	return FRingBufferTest::RunTest(Parameters);
}


#endif // #if WITH_DEV_AUTOMATION_TESTS