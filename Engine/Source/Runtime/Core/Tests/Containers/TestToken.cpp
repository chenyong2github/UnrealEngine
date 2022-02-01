// Copyright Epic Games, Inc. All Rights Reserved.

#include "TestToken.h"
#include "TestHarness.h"

TEST_CASE("Core::Containers::TToken::Default", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		int32Token Token;
		REQUIRE(*Token == 0);
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls());
	REQUIRE(int32Token::NumConstructorCalls() == 1);
	REQUIRE(int32Token::NumCopyConstructorCalls() == 0);
	REQUIRE(int32Token::NumMoveConstructorCalls() == 0);
	REQUIRE(int32Token::NumConstructionCalls() == 1);
	REQUIRE(int32Token::NumDestructionCalls() == 1);
}

TEST_CASE("Core::Containers::TToken::Explicit constructor", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		int32Token Token = int32Token(1);
		REQUIRE(*Token == 1);
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(1));
	REQUIRE(int32Token::NumCopyCalls() == 0);
	REQUIRE(int32Token::NumMoveCalls() == 0);
	REQUIRE(int32Token::NumConstructorCalls() == 1);
	REQUIRE(int32Token::NumCopyConstructorCalls() == 0);
	REQUIRE(int32Token::NumMoveConstructorCalls() == 0);
	REQUIRE(int32Token::NumCopyAssignmentCalls() == 0);
	REQUIRE(int32Token::NumMoveAssignmentCalls() == 0);
}

TEST_CASE("Core::Containers::TToken::Copy constructor", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		int32Token TempToken(2);
		int32Token Token = TempToken;
		REQUIRE(*Token == 2);
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(2));
	REQUIRE(int32Token::NumCopyCalls() == 1);
	REQUIRE(int32Token::NumMoveCalls() == 0);
	REQUIRE(int32Token::NumConstructorCalls() == 1);
	REQUIRE(int32Token::NumCopyConstructorCalls() == 1);
	REQUIRE(int32Token::NumMoveConstructorCalls() == 0);
	REQUIRE(int32Token::NumCopyAssignmentCalls() == 0);
	REQUIRE(int32Token::NumMoveAssignmentCalls() == 0);
}

TEST_CASE("Core::Containers::TToken::Move constructor", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		int32Token TempToken(3);
		int32Token Token = MoveTemp(TempToken);
		REQUIRE(*Token == 3);
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(2));
	REQUIRE(int32Token::NumCopyCalls() == 0);
	REQUIRE(int32Token::NumMoveCalls() == 1);
	REQUIRE(int32Token::NumConstructorCalls() == 1);
	REQUIRE(int32Token::NumCopyConstructorCalls() == 0);
	REQUIRE(int32Token::NumMoveConstructorCalls() == 1);
	REQUIRE(int32Token::NumCopyAssignmentCalls() == 0);
	REQUIRE(int32Token::NumMoveAssignmentCalls() == 0);
}

TEST_CASE("Core::Containers::TToken::Copy assignment", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		int32Token TempToken(4);
		int32Token Token;
		Token = TempToken;
		REQUIRE(*Token == 4);
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(2));
	REQUIRE(int32Token::NumCopyCalls() == 1);
	REQUIRE(int32Token::NumMoveCalls() == 0);
	REQUIRE(int32Token::NumConstructorCalls() == 2);
	REQUIRE(int32Token::NumCopyConstructorCalls() == 0);
	REQUIRE(int32Token::NumMoveConstructorCalls() == 0);
	REQUIRE(int32Token::NumCopyAssignmentCalls() == 1);
	REQUIRE(int32Token::NumMoveAssignmentCalls() == 0);
}

TEST_CASE("Core::Containers::TToken::Move assignment", "[Core][Containers][Smoke]")
{
	int32Token::Reset();
	{
		int32Token TempToken(5);
		int32Token Token;
		Token = MoveTemp(TempToken);
		REQUIRE(*Token == 5);
	}
	REQUIRE(int32Token::EvenConstructionDestructionCalls(2));
	REQUIRE(int32Token::NumCopyCalls() == 0);
	REQUIRE(int32Token::NumMoveCalls() == 1);
	REQUIRE(int32Token::NumConstructorCalls() == 2);
	REQUIRE(int32Token::NumCopyConstructorCalls() == 0);
	REQUIRE(int32Token::NumMoveConstructorCalls() == 0);
	REQUIRE(int32Token::NumCopyAssignmentCalls() == 0);
	REQUIRE(int32Token::NumMoveAssignmentCalls() == 1);
}
