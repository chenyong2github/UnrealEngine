// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_TESTS

#include "Algo/Unique.h"
#include "Containers/Array.h"

#include "Tests/TestHarnessAdapter.h"

TEST_CASE_NAMED(FUniqueTest, "System::Core::Algo::Unique", "[ApplicationContextMask][SmokeFilter]")
{
	using namespace Algo;

	{
		TArray<int32> Array;
		int32 RemoveFrom = Unique(Array);
		CHECK_MESSAGE(TEXT("`Unique` must handle an empty container"), RemoveFrom == 0);
	}
	{
		TArray<int32> Array{ 1, 2, 3 };
		Array.SetNum(Unique(Array));
		CHECK_MESSAGE(TEXT("Uniqued container with no duplicates must remain unchanged"), (Array == TArray<int32>{1, 2, 3}));
	}
	{
		TArray<int32> Array{ 1, 1, 2, 2, 2, 3, 3, 3, 3 };
		Array.SetNum(Unique(Array));
		CHECK_MESSAGE(TEXT("`Unique` with multiple duplicates must return correct result"), (Array == TArray<int32>{1, 2, 3}));
	}
	{
		TArray<int32> Array{ 1, 1, 2, 3, 3, 3 };
		Array.SetNum(Unique(Array));
		CHECK_MESSAGE(TEXT("`Unique` with duplicates and unique items must return correct result"), (Array == TArray<int32>{1, 2, 3}));
	}
	{
		FString Str = TEXT("aa");
		Str = Str.Mid(0, Unique(Str));
		CHECK_MESSAGE(TEXT("`Unique` on `FString` as an example of arbitrary random-access container must compile and return correct result"),
			Str == FString(TEXT("a")));
	}
	{
		int32 Array[] = { 1 };
		int32 NewSize = (int32)Unique(Array);
		CHECK_MESSAGE(TEXT("`Unique` must support C arrays"), NewSize == 1);
	}
	{
		TArray<int32> Array = { 1, 1 };
		int32 NewSize = Unique(MakeArrayView(Array.GetData() + 1, 1));
		CHECK_MESSAGE(TEXT("`Unique` must support ranges"), NewSize == 1);
	}
}

#endif // WITH_TESTS
