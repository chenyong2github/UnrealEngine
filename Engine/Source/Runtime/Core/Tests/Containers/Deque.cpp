// Copyright Epic Games, Inc. All Rights Reserved.

#include "Containers/Deque.h"
#include "CoreMinimal.h"
#include "TestToken.h"
#include "TestUtils.h"
#include "TestHarness.h"

namespace Deque
{
namespace Test
{
static constexpr int32 DefaultCapacity = 4;

/**
 * Emplaces the parameter Count elements into the parameter queue and pops them one by one validating FIFO ordering.
 * This method also verifies ther correctness of: Last() and First()
 */
bool EmplaceLastPopFirst(TDeque<int32Token>& Deque);
bool EmplaceLastPopFirst(TDeque<int32Token>& Deque, int32 Count);
bool EmplaceFirstPopLast(TDeque<int32Token>& Deque);
bool EmplaceFirstPopLast(TDeque<int32Token>& Deque, int32 Count);

}  // namespace Test

//---------------------------------------------------------------------------------------------------------------------
// Deque::Test
//---------------------------------------------------------------------------------------------------------------------

bool Test::EmplaceLastPopFirst(TDeque<int32Token>& Deque)
{
	ensure(Deque.Max());
	return EmplaceLastPopFirst(Deque, Deque.Max());
}

bool Test::EmplaceLastPopFirst(TDeque<int32Token>& Deque, int32 Count)
{
	const int32 SeedValue = FMath::RandRange(1, 999);
	for (int32 i = 0; i < Count; ++i)
	{
		Deque.EmplaceLast(SeedValue + i);
		REQUIRE(Deque.Num() == i + 1);
		REQUIRE(Deque.First() == SeedValue);
		REQUIRE(Deque.Last() == SeedValue + i);
	}
	for (int32 i = 0; i < Count; ++i)
	{
		REQUIRE(Deque.First() == SeedValue + i);
		REQUIRE(Deque.Last() == SeedValue + Count - 1);
		Deque.PopFirst();
		REQUIRE(Deque.Num() == Count - (i + 1));
	}
	return true;
}

bool Test::EmplaceFirstPopLast(TDeque<int32Token>& Deque)
{
	ensure(Deque.Max());
	return EmplaceFirstPopLast(Deque, Deque.Max());
}

bool Test::EmplaceFirstPopLast(TDeque<int32Token>& Deque, int32 Count)
{
	const int32 SeedValue = FMath::RandRange(1, 999);
	for (int32 i = 0; i < Count; ++i)
	{
		Deque.EmplaceFirst(SeedValue + i);
		REQUIRE(Deque.Num() == i + 1);
		REQUIRE(Deque.First() == SeedValue + i);
		REQUIRE(Deque.Last() == SeedValue);
	}
	for (int32 i = 0; i < Count; ++i)
	{
		REQUIRE(Deque.First() == SeedValue + Count - 1);
		REQUIRE(Deque.Last() == SeedValue + i);
		Deque.PopLast();
		REQUIRE(Deque.Num() == Count - (i + 1));
	}
	return true;
}

//---------------------------------------------------------------------------------------------------------------------
// Unit tests
//---------------------------------------------------------------------------------------------------------------------

TEST_CASE("Core::Containers::TDeque::Reserve without data", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	TDeque<int32Token> Deque;
	REQUIRE(!Deque.Max());
	REQUIRE(!Deque.Num());
	REQUIRE(Deque.IsEmpty());
	Deque.Reserve(Test::DefaultCapacity);
	REQUIRE(Deque.Max() >= Test::DefaultCapacity);
	REQUIRE(!Deque.Num());
	REQUIRE(Deque.IsEmpty());
	REQUIRE(int32Token::EvenConstructionDestructionCalls(0));
}

TEST_CASE("Core::Containers::TDeque::Reserve EmplaceLast single element", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque;
		REQUIRE(!Deque.Max());
		REQUIRE(!Deque.Num());
		Deque.Reserve(Test::DefaultCapacity);
		Deque.EmplaceLast(0);
		REQUIRE(Deque.Max() >= Test::DefaultCapacity);
		REQUIRE(Deque.Max() < Test::DefaultCapacity * 2);
		REQUIRE(Deque.Num() == 1);
		Deque.Reserve(Test::DefaultCapacity * 2);
		REQUIRE(Deque.Max() >= Test::DefaultCapacity * 2);
		REQUIRE(Deque.Num() == 1);
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(1));
}

TEST_CASE("Core::Containers::TDeque::Reset", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque;
		Deque.Reset();	// Should be innocuous
		REQUIRE(!Deque.Max());
		REQUIRE(!Deque.Num());
		REQUIRE(Deque.IsEmpty());
		Deque.EmplaceLast(0);
		REQUIRE(Deque.Max());
		REQUIRE(Deque.Num() == 1);
		REQUIRE(!Deque.IsEmpty());
		Deque.Reset();
		REQUIRE(Deque.Max());
		REQUIRE(!Deque.Num());
		REQUIRE(Deque.IsEmpty());
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(1));
}

TEST_CASE("Core::Containers::TDeque::Empty", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque;
		Deque.Empty();	// Should be innocuous
		REQUIRE(!Deque.Max());
		REQUIRE(!Deque.Num());
		REQUIRE(Deque.IsEmpty());
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(0));
}

TEST_CASE("Core::Containers::TDeque::Empty after single element EmplaceLast", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque;
		Deque.EmplaceLast(0);
		REQUIRE(Deque.Max());
		REQUIRE(Deque.Num() == 1);
		REQUIRE(!Deque.IsEmpty());
		Deque.Empty();
		REQUIRE(!Deque.Max());
		REQUIRE(!Deque.Num());
		REQUIRE(Deque.IsEmpty());
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(1));
}

TEST_CASE("Core::Containers::TDeque::EmplaceLast single element", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque;
		REQUIRE(!Deque.Max());
		REQUIRE(!Deque.Num());
		REQUIRE(Deque.IsEmpty());
		Deque.EmplaceLast(0);
		REQUIRE(Deque.Max());
		REQUIRE(Deque.Num() == 1);
		REQUIRE(!Deque.IsEmpty());
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(1));
}

TEST_CASE("Core::Containers::TDeque::EmplaceLast range to capacity", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque;
		REQUIRE(!Deque.Max());
		REQUIRE(!Deque.Num());
		Deque.Reserve(Test::DefaultCapacity * 10);
		REQUIRE(Deque.Max() == Test::DefaultCapacity * 10);
		while (Deque.Num() < Deque.Max())
		{
			Deque.EmplaceLast();
		}
		REQUIRE(Deque.Max() == Deque.Num());
		REQUIRE(Deque.Max() == Test::DefaultCapacity * 10);
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(Test::DefaultCapacity * 10));
}

TEST_CASE("Core::Containers::TDeque::EmplaceLast range past capacity", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque;
		REQUIRE(!Deque.Max());
		REQUIRE(!Deque.Num());
		Deque.Reserve(Test::DefaultCapacity);
		REQUIRE(Deque.Max() == Test::DefaultCapacity);
		while (Deque.Num() < Deque.Max())
		{
			Deque.EmplaceLast();
		}
		REQUIRE(Deque.Max() == Deque.Num());
		REQUIRE(Deque.Max() == Test::DefaultCapacity);
		Deque.EmplaceLast();
		REQUIRE(Deque.Max() > Deque.Num());
		REQUIRE(Deque.Max() > Test::DefaultCapacity);
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(Test::DefaultCapacity + 1));
}

TEST_CASE("Core::Containers::TDeque::EmplaceFirst single element", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque;
		REQUIRE(!Deque.Max());
		REQUIRE(!Deque.Num());
		REQUIRE(Deque.IsEmpty());
		Deque.EmplaceFirst(0);
		REQUIRE(Deque.Max());
		REQUIRE(Deque.Num() == 1);
		REQUIRE(!Deque.IsEmpty());
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(1));
}

TEST_CASE("Core::Containers::TDeque::EmplaceFirst range to capacity", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque;
		REQUIRE(!Deque.Max());
		REQUIRE(!Deque.Num());
		Deque.Reserve(Test::DefaultCapacity * 10);
		REQUIRE(Deque.Max() == Test::DefaultCapacity * 10);
		while (Deque.Num() < Deque.Max())
		{
			Deque.EmplaceFirst();
		}
		REQUIRE(Deque.Max() == Deque.Num());
		REQUIRE(Deque.Max() == Test::DefaultCapacity * 10);
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(Test::DefaultCapacity * 10));
}

TEST_CASE("Core::Containers::TDeque::PushLast single element (implicit move)", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque;
		REQUIRE(!Deque.Max());
		REQUIRE(!Deque.Num());
		REQUIRE(Deque.IsEmpty());
		Deque.PushLast(0);	// implicit conversion from temporary
		REQUIRE(Deque.Max());
		REQUIRE(Deque.Num() == 1);
		REQUIRE(!Deque.IsEmpty());
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(2));
	REQUIRE(int32Token::NumConstructorCalls() == 1);
	REQUIRE(int32Token::NumCopyConstructorCalls() == 0);
	REQUIRE(int32Token::NumMoveConstructorCalls() == 1);
}

TEST_CASE("Core::Containers::TDeque::PushLast single element from move", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque;
		REQUIRE(!Deque.Max());
		REQUIRE(!Deque.Num());
		REQUIRE(Deque.IsEmpty());
		int32Token TempToken;
		Deque.PushLast(MoveTemp(TempToken));
		REQUIRE(Deque.Max());
		REQUIRE(Deque.Num() == 1);
		REQUIRE(!Deque.IsEmpty());
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(2));
	REQUIRE(int32Token::NumConstructorCalls() == 1);
	REQUIRE(int32Token::NumCopyConstructorCalls() == 0);
	REQUIRE(int32Token::NumMoveConstructorCalls() == 1);
}

TEST_CASE("Core::Containers::TDeque::PushLast single element from copy", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque;
		REQUIRE(!Deque.Max());
		REQUIRE(!Deque.Num());
		REQUIRE(Deque.IsEmpty());
		int32Token TempToken;
		Deque.PushLast(TempToken);
		REQUIRE(Deque.Max());
		REQUIRE(Deque.Num() == 1);
		REQUIRE(!Deque.IsEmpty());
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(2));
	REQUIRE(int32Token::NumConstructorCalls() == 1);
	REQUIRE(int32Token::NumCopyConstructorCalls() == 1);
	REQUIRE(int32Token::NumMoveConstructorCalls() == 0);
}

TEST_CASE("Core::Containers::TDeque::PushFirst single element (implicit move)", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque;
		REQUIRE(!Deque.Max());
		REQUIRE(!Deque.Num());
		REQUIRE(Deque.IsEmpty());
		Deque.PushFirst(0);	 // implicit conversion from temporary
		REQUIRE(Deque.Max());
		REQUIRE(Deque.Num() == 1);
		REQUIRE(!Deque.IsEmpty());
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(2));
	REQUIRE(int32Token::NumConstructorCalls() == 1);
	REQUIRE(int32Token::NumCopyConstructorCalls() == 0);
	REQUIRE(int32Token::NumMoveConstructorCalls() == 1);
}

TEST_CASE("Core::Containers::TDeque::PushFirst single element from move", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque;
		REQUIRE(!Deque.Max());
		REQUIRE(!Deque.Num());
		REQUIRE(Deque.IsEmpty());
		int32Token TempToken;
		Deque.PushFirst(MoveTemp(TempToken));
		REQUIRE(Deque.Max());
		REQUIRE(Deque.Num() == 1);
		REQUIRE(!Deque.IsEmpty());
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(2));
	REQUIRE(int32Token::NumConstructorCalls() == 1);
	REQUIRE(int32Token::NumCopyConstructorCalls() == 0);
	REQUIRE(int32Token::NumMoveConstructorCalls() == 1);
}

TEST_CASE("Core::Containers::TDeque::PushFirst single element from copy", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque;
		REQUIRE(!Deque.Max());
		REQUIRE(!Deque.Num());
		REQUIRE(Deque.IsEmpty());
		int32Token TempToken;
		Deque.PushFirst(TempToken);
		REQUIRE(Deque.Max());
		REQUIRE(Deque.Num() == 1);
		REQUIRE(!Deque.IsEmpty());
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(2));
	REQUIRE(int32Token::NumConstructorCalls() == 1);
	REQUIRE(int32Token::NumCopyConstructorCalls() == 1);
	REQUIRE(int32Token::NumMoveConstructorCalls() == 0);
}

void PopOne(TDeque<int32Token>& Deque)
{
	Deque.PopFirst();
}

TEST_CASE("Core::Containers::TDeque::EmplaceLast/PopFirst single element", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque;
		REQUIRE(!Deque.Max());
		REQUIRE(!Deque.Num());
		REQUIRE(Deque.IsEmpty());
		Deque.EmplaceLast(0);
		REQUIRE(Deque.Max());
		REQUIRE(Deque.Num() == 1);
		REQUIRE(!Deque.IsEmpty());
		PopOne(Deque);
		REQUIRE(Deque.Max());
		REQUIRE(!Deque.Num());
		REQUIRE(Deque.IsEmpty());
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(1));
}

TEST_CASE("Core::Containers::TDeque::EmplaceLast/PopFirst single element multiple times causing head/tail wrap around", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque;
		Deque.Reserve(Test::DefaultCapacity);
		REQUIRE(Deque.Max() == Test::DefaultCapacity);
		for (int32 i = 0; i < Test::DefaultCapacity * 2; ++i)
		{
			REQUIRE(Test::EmplaceLastPopFirst(Deque, 1));  // Rotates head and tail
			REQUIRE(Deque.Max() == Deque.Max());
		}
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(Test::DefaultCapacity * 2));
}

TEST_CASE("Core::Containers::TDeque::EmplaceLast/PopFirst range without head/tail wrap around", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque;
		Deque.Reserve(Test::DefaultCapacity);
		REQUIRE(Test::EmplaceLastPopFirst(Deque));
		REQUIRE(Deque.Max() == Test::DefaultCapacity);
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(Test::DefaultCapacity));
}

TEST_CASE("Core::Containers::TDeque::EmplaceLast/PopFirst range with reallocation without head/tail wrap around", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque;
		Deque.Reserve(Test::DefaultCapacity);
		REQUIRE(Deque.Max() == Test::DefaultCapacity);
		REQUIRE(Test::EmplaceLastPopFirst(Deque, Test::DefaultCapacity + 1));
		REQUIRE(Deque.Max() > Test::DefaultCapacity);
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(Test::DefaultCapacity + 1));
}

TEST_CASE("Core::Containers::TDeque::EmplaceLast/PopFirst range with head/tail wrap around", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque;
		Deque.Reserve(Test::DefaultCapacity);
		for (int32 i = 0; i < Test::DefaultCapacity; ++i)
		{
			REQUIRE(Test::EmplaceLastPopFirst(Deque, Test::DefaultCapacity - 1));  // Rotates head and tail
			REQUIRE(Deque.Max() == Test::DefaultCapacity);
		}
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(Test::DefaultCapacity * (Test::DefaultCapacity - 1)));
}

TEST_CASE("Core::Containers::TDeque::EmplaceFirst/PopLast single element", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque;
		REQUIRE(!Deque.Max());
		REQUIRE(!Deque.Num());
		REQUIRE(Deque.IsEmpty());
		Deque.EmplaceFirst(0);
		REQUIRE(Deque.Max());
		REQUIRE(Deque.Num() == 1);
		REQUIRE(!Deque.IsEmpty());
		Deque.PopLast();
		REQUIRE(Deque.Max());
		REQUIRE(!Deque.Num());
		REQUIRE(Deque.IsEmpty());
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(1));
}

TEST_CASE("Core::Containers::TDeque::EmplaceFirst/PopLast range", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque;
		Deque.Reserve(Test::DefaultCapacity);
		REQUIRE(Test::EmplaceFirstPopLast(Deque));
		REQUIRE(Deque.Max() == Test::DefaultCapacity);
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(Test::DefaultCapacity));
}

TEST_CASE("Core::Containers::TDeque::EmplaceFirst/PopLast range with reallocation", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque;
		Deque.Reserve(Test::DefaultCapacity);
		REQUIRE(Deque.Max() == Test::DefaultCapacity);
		REQUIRE(Test::EmplaceFirstPopLast(Deque, Test::DefaultCapacity + 1));
		REQUIRE(Deque.Max() > Test::DefaultCapacity);
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(Test::DefaultCapacity + 1));
}

TEST_CASE("Core::Containers::TDeque::TryPopFirst", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque;
		Deque.Reserve(Test::DefaultCapacity);
		REQUIRE(Deque.Max() == Test::DefaultCapacity);
		for (int32 i = 0; i < Test::DefaultCapacity; ++i)
		{
			Deque.EmplaceLast(i);
			REQUIRE(Deque.Max() == Test::DefaultCapacity);
		}
		int32Token Result;
		int CheckValue = 0;
		while (Deque.TryPopFirst(Result))
		{
			REQUIRE(*Result == CheckValue++);
		}
		REQUIRE(CheckValue == Test::DefaultCapacity);
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(Test::DefaultCapacity + 1));  // + 1 for Result
}

TEST_CASE("Core::Containers::TDeque::TryPopFirst with reallocation", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque;
		Deque.Reserve(Test::DefaultCapacity);
		REQUIRE(Deque.Max() == Test::DefaultCapacity);
		for (int32 i = 0; i < Test::DefaultCapacity; ++i)
		{
			Deque.EmplaceLast(i);
			REQUIRE(Deque.Max() == Test::DefaultCapacity);
		}
		Deque.EmplaceLast(Test::DefaultCapacity);
		REQUIRE(Deque.Max() > Test::DefaultCapacity);

		int32Token Result;
		int32 CheckValue = 0;
		while (Deque.TryPopFirst(Result))
		{
			REQUIRE(*Result == CheckValue++);
		}
		REQUIRE(CheckValue == Test::DefaultCapacity + 1);
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(Test::DefaultCapacity + 2));
}

TEST_CASE("Core::Containers::TDeque::TryPopLast", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque;
		Deque.Reserve(Test::DefaultCapacity);
		REQUIRE(Deque.Max() == Test::DefaultCapacity);
		for (int32 i = 0; i < Test::DefaultCapacity; ++i)
		{
			Deque.EmplaceFirst(i);
			REQUIRE(Deque.Max() == Test::DefaultCapacity);
		}
		int32Token Result;
		int CheckValue = 0;
		while (Deque.TryPopLast(Result))
		{
			REQUIRE(*Result == CheckValue++);
		}
		REQUIRE(CheckValue == Test::DefaultCapacity);
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(Test::DefaultCapacity + 1));  // + 1 for Result
}

TEST_CASE("Core::Containers::TDeque::TryPopLast with reallocation", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque;
		Deque.Reserve(Test::DefaultCapacity);
		REQUIRE(Deque.Max() == Test::DefaultCapacity);
		for (int32 i = 0; i < Test::DefaultCapacity; ++i)
		{
			Deque.EmplaceFirst(i);
			REQUIRE(Deque.Max() == Test::DefaultCapacity);
		}
		Deque.EmplaceFirst(Test::DefaultCapacity);
		REQUIRE(Deque.Max() > Test::DefaultCapacity);

		int32Token Result;
		int32 CheckValue = 0;
		while (Deque.TryPopLast(Result))
		{
			REQUIRE(*Result == CheckValue++);
		}
		REQUIRE(CheckValue == Test::DefaultCapacity + 1);
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(Test::DefaultCapacity + 2));
}

TEST_CASE("Core::Containers::TDeque::Comparison simple", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque, DestQueue;
		Deque.Reserve(Test::DefaultCapacity);
		DestQueue.Reserve(Test::DefaultCapacity);
		REQUIRE(Deque.Max() == Test::DefaultCapacity);
		for (int32 i = 0; i < Test::DefaultCapacity; ++i)
		{
			Deque.EmplaceLast(i);
			DestQueue.EmplaceLast(i);
			REQUIRE(Deque.Max() == Test::DefaultCapacity);
		}
		REQUIRE(Deque == DestQueue);
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(Test::DefaultCapacity * 2));
}

TEST_CASE("Core::Containers::TDeque::Comparison with head/tail wrap around", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque, DestQueue;
		Deque.Reserve(Test::DefaultCapacity);
		DestQueue.Reserve(Test::DefaultCapacity);
		REQUIRE(Deque.Max() == Test::DefaultCapacity);
		for (int32 i = 0; i < Test::DefaultCapacity; ++i)
		{
			Deque.EmplaceLast(i);
			REQUIRE(Deque.Max() == Test::DefaultCapacity);
		}
		for (int32 Iteration = 0; Iteration < Test::DefaultCapacity; ++Iteration)
		{
			REQUIRE(Test::EmplaceLastPopFirst(DestQueue, 1));  // Rotates head and tail
			for (int32 i = 0; i < Test::DefaultCapacity; ++i)
			{
				DestQueue.EmplaceLast(i);
				REQUIRE(DestQueue.Max() == Test::DefaultCapacity);
			}
			REQUIRE(Deque == DestQueue);
			DestQueue.Reset();
		}
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(Test::DefaultCapacity * 6));
}

TEST_CASE("Core::Containers::TDeque::Copy simple", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque;
		Deque.Reserve(Test::DefaultCapacity);
		REQUIRE(Deque.Max() == Test::DefaultCapacity);
		for (int32 i = 0; i < Test::DefaultCapacity; ++i)
		{
			Deque.EmplaceLast(i);
			REQUIRE(Deque.Max() == Test::DefaultCapacity);
		}
		TDeque<int32Token> DestQueue(Deque);
		REQUIRE(Deque == DestQueue);
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(Test::DefaultCapacity * 2));
}

TEST_CASE("Core::Containers::TDeque::Copy with head/tail wrap around", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque;
		Deque.Reserve(Test::DefaultCapacity);
		REQUIRE(Deque.Max() == Test::DefaultCapacity);
		for (int32 Iteration = 0; Iteration < Test::DefaultCapacity; ++Iteration)
		{
			REQUIRE(Test::EmplaceLastPopFirst(Deque, 1));  // Rotates head and tail
			for (int32 i = 0; i < Test::DefaultCapacity; ++i)
			{
				Deque.EmplaceLast(i);
				REQUIRE(Deque.Max() == Test::DefaultCapacity);
			}
			TDeque<int32Token> DestQueue(Deque);
			REQUIRE(Deque == DestQueue);
			Deque.Reset();
		}
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(Test::DefaultCapacity * 9));
}

TEST_CASE("Core::Containers::TDeque::Copy variable size with head/tail wrap around", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque;
		Deque.Reserve(Test::DefaultCapacity);
		REQUIRE(Deque.Max() == Test::DefaultCapacity);
		for (int32 Iteration = 0; Iteration < Test::DefaultCapacity; ++Iteration)
		{
			for (int32 Size = 1; Size <= Test::DefaultCapacity; ++Size)
			{
				REQUIRE(Test::EmplaceLastPopFirst(Deque, 1));  // Rotates head and tail
				const int32 SeedValue = FMath::RandRange(1, 999);
				for (int32 i = 0; i < Size; ++i)
				{
					Deque.EmplaceLast(SeedValue + i);
					REQUIRE(Deque.Max() == Test::DefaultCapacity);
				}
				TDeque<int32Token> DestQueue(Deque);
				REQUIRE(Deque == DestQueue);
				REQUIRE(DestQueue.Max() <= Test::DefaultCapacity);
				Deque.Reset();
			}
		}
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls());
}

TEST_CASE("Core::Containers::TDeque::Move simple", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque;
		Deque.Reserve(Test::DefaultCapacity);
		REQUIRE(Deque.Max() == Test::DefaultCapacity);
		for (int32 i = 0; i < Test::DefaultCapacity; ++i)
		{
			Deque.EmplaceLast(i);
			REQUIRE(Deque.Max() == Test::DefaultCapacity);
		}
		TDeque<int32Token> DestQueue(MoveTemp(Deque));
		REQUIRE(Deque.IsEmpty());
		int32Token Result;
		int CheckValue = 0;
		while (Deque.TryPopFirst(Result))
		{
			REQUIRE(*Result == CheckValue++);
		}
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(Test::DefaultCapacity + 1));
}

TEST_CASE("Core::Containers::TDeque::Move with head/tail wrap around", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque;
		Deque.Reserve(Test::DefaultCapacity);
		REQUIRE(Deque.Max() == Test::DefaultCapacity);
		for (int32 Iteration = 0; Iteration < Test::DefaultCapacity; ++Iteration)
		{
			REQUIRE(Test::EmplaceLastPopFirst(Deque, 1));  // Rotates head and tail
			for (int32 i = 0; i < Test::DefaultCapacity; ++i)
			{
				Deque.EmplaceLast(i);
				REQUIRE(Deque.Max() == Test::DefaultCapacity);
			}
			TDeque<int32Token> DestQueue(MoveTemp(Deque));
			REQUIRE(Deque.IsEmpty());
			int32Token Result;
			int32 CheckValue = 0;
			while (Deque.TryPopFirst(Result))
			{
				REQUIRE(*Result == CheckValue++);
			}
		}
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(Test::DefaultCapacity * 6));
}

TEST_CASE("Core::Containers::TDeque::Move variable size with head/tail wrap around", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque;
		Deque.Reserve(Test::DefaultCapacity);
		REQUIRE(Deque.Max() == Test::DefaultCapacity);
		for (int32 Iteration = 0; Iteration < Test::DefaultCapacity; ++Iteration)
		{
			for (int32 Size = 1; Size <= Test::DefaultCapacity; ++Size)
			{
				REQUIRE(Test::EmplaceLastPopFirst(Deque, 1));  // Rotates head and tail
				const int32 SeedValue = FMath::RandRange(1, 999);
				for (int32 i = 0; i < Size; ++i)
				{
					Deque.EmplaceLast(SeedValue + i);
					REQUIRE(Deque.Max() == Test::DefaultCapacity);
				}
				TDeque<int32Token> DestQueue(MoveTemp(Deque));
				REQUIRE(Deque.IsEmpty());
				int32Token Result;
				int32 CheckValue = SeedValue;
				while (Deque.TryPopFirst(Result))
				{
					REQUIRE(*Result == CheckValue++);
				}
			}
		}
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls());
}

TEST_CASE("Core::Containers::TDeque::Iteration without head/tail wrap around", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque;
		Deque.Reserve(Test::DefaultCapacity);
		REQUIRE(Deque.Max() == Test::DefaultCapacity);
		for (int32 i = 0; i < Test::DefaultCapacity; ++i)
		{
			Deque.EmplaceLast(i);
			REQUIRE(Deque.Max() == Test::DefaultCapacity);
		}
		for (int32 i = 0; i < Test::DefaultCapacity; ++i)
		{
			REQUIRE(Deque[i] == i);
		}
		int32 CheckValue = 0;
		for (const auto& Value : Deque)
		{
			REQUIRE(Value == CheckValue++);
		}
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(Test::DefaultCapacity));
}

TEST_CASE("Core::Containers::TDeque::Iteration with head/tail wrap around", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque;
		Deque.Reserve(Test::DefaultCapacity);
		REQUIRE(Deque.Max() == Test::DefaultCapacity);
		for (int32 Iteration = 0; Iteration < Test::DefaultCapacity; ++Iteration)
		{
			REQUIRE(Test::EmplaceLastPopFirst(Deque, 1));  // Rotates head and tail
			for (int32 i = 0; i < Test::DefaultCapacity; ++i)
			{
				Deque.EmplaceLast(i);
				REQUIRE(Deque.Max() == Test::DefaultCapacity);
			}
			for (int32 i = 0; i < Test::DefaultCapacity; ++i)
			{
				REQUIRE(Deque[i] == i);
			}
			int32 CheckValue = 0;
			for (const auto& Value : Deque)
			{
				REQUIRE(Value == CheckValue++);
			}
			Deque.Reset();
		}
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(Test::DefaultCapacity * (Test::DefaultCapacity + 1)));
}

TEST_CASE("Core::Containers::TDeque::Iterator arithmetic", "[Core][Containers][Smoke]")
{
	TDeque<int32Token> Deque;
	Deque.Reserve(Test::DefaultCapacity);
	REQUIRE(Deque.Max() == Test::DefaultCapacity);
	for (int32 Iteration = 0; Iteration < Test::DefaultCapacity; ++Iteration)
	{
		REQUIRE(Test::EmplaceLastPopFirst(Deque, 1));  // Rotates head and tail
		Deque.EmplaceLast(13);
		Deque.EmplaceLast(42);
		Deque.EmplaceLast(19);

		auto It = Deque.begin();
		REQUIRE(**It == 13);
		REQUIRE(*It == int32Token(13));
		REQUIRE(It->Value == 13);
		auto It2 = It++;
		REQUIRE(It2 != It);
		REQUIRE(It2->Value == 13);
		REQUIRE(**It == 42);
		REQUIRE(*It == int32Token(42));
		REQUIRE(It->Value == 42);

		Deque.Reset();
	}
}

TEST_CASE("Core::Containers::TDeque::Construct from std initializer_list", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque({0, 1, 2, 3, 4, 5});
		int32Token Result;
		int32 CheckValue = 0;
		while (Deque.TryPopFirst(Result))
		{
			REQUIRE(*Result == CheckValue++);
		}
		REQUIRE(CheckValue == 6);
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(6 * 2 + 1));
}

TEST_CASE("Core::Containers::TDeque::Construct from empty std initializer_list", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque({});
		REQUIRE(Deque.IsEmpty());
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(0));
}

TEST_CASE("Core::Containers::TDeque::Assign from std initializer_list", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		TDeque<int32Token> Deque;
		Deque.EmplaceLast(0);
		Deque = {0, 1, 2, 3, 4, 5};
		int32Token Result;
		int32 CheckValue = 0;
		while (Deque.TryPopFirst(Result))
		{
			REQUIRE(*Result == CheckValue++);
		}
		REQUIRE(CheckValue == 6);
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(6 * 2 + 2));
}

}  // namespace Deque
