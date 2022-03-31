// Copyright Epic Games, Inc. All Rights Reserved.

#include "Containers/UnrealString.h"
#include "Algo/Unique.h"
#include "Containers/Array.h"
#include "TestHarness.h"

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Algorithm::Unique::Smoke Test", "[Core][Algorithm][Smoke]")
{
	using namespace Algo;

	{
		TArray<int32> Array;
		int32 RemoveFrom = Unique(Array);
		INFO("`Unique` must handle an empty container");
		CHECK(RemoveFrom==0);
	}
	{
		TArray<int32> Array{ 1, 2, 3 };
		Array.SetNum(Unique(Array));
		INFO("Uniqued container with no duplicates must remain unchanged");
		CHECK(Array==TArray<int32>{1, 2, 3});
	}
	{
		TArray<int32> Array{ 1, 1, 2, 2, 2, 3, 3, 3, 3};
		Array.SetNum(Unique(Array));
		INFO("`Unique` with multiple duplicates must return correct result");
		CHECK(Array==TArray<int32>{1, 2, 3});
	}
	{
		TArray<int32> Array{ 1, 1, 2, 3, 3, 3 };
		Array.SetNum(Unique(Array));
		INFO("`Unique` with duplicates and unique items must return correct result");
		CHECK(Array==TArray<int32>{1, 2, 3});
	}
	{
		FString Str = TEXT("aa");
		Str = Str.Mid(0, Unique(Str));
		INFO("`Unique` on `FString` as an example of arbitrary random-access container must compile and return correct result");
		CHECK(Str==FString(TEXT("a")));
	}
	{
		int32 Array[] = {1};
		int32 NewSize = (int32)Unique(Array);
		INFO("`Unique` must support C arrays");
		CHECK(NewSize==1);
	}
	{
		TArray<int32> Array = { 1, 1 };
		int32 NewSize = Unique(MakeArrayView(Array.GetData() + 1, 1));
		INFO("`Unique` must support ranges");
		CHECK(NewSize==1);
	}
}
