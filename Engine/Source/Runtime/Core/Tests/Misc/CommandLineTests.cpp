// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_LOW_LEVEL_TESTS

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"

#include "Tests/TestHarnessAdapter.h"

#include <catch2/generators/catch_generators.hpp>

TEST_CASE("CommandLine::Filtering::FilterMove", "[Smoke]")
{
	TArray<FString> AllowedList{ { "cmd_a", "-cmd_b" } };

	SECTION("Filtering CLI examples")
	{
		auto [Input, Expected] = GENERATE_COPY(table<const TCHAR*, const TCHAR*>(
		{
			{ TEXT(""), TEXT("")},
			{ TEXT("not_on_this_list"), TEXT("") },
			{ TEXT("-cmd_a --cmd_b"), TEXT("-cmd_a --cmd_b") },
			{ TEXT("-cmd_a --cmd_b not_on_this_list"), TEXT("-cmd_a --cmd_b") },
			{ TEXT("-cmd_a not_on_this_list --cmd_b"), TEXT("-cmd_a --cmd_b") },
			{ TEXT("-cmd_a -cmd_a -cmd_a"), TEXT("-cmd_a -cmd_a -cmd_a") },
			{ TEXT("-cmd_a --cmd_b \"-cmd_a --cmd_b not_on_this_list\""), TEXT("-cmd_a --cmd_b -cmd_a --cmd_b") },
			{ TEXT("-cmd_a=1 not_on_this_list=2 --cmd_b=true "), TEXT("-cmd_a=1 --cmd_b=true") },
			{ TEXT("-cmd_a=  not_on_this_list=2 --cmd_b=true "), TEXT("-cmd_a=not_on_this_list=2 --cmd_b=true") },
			{ TEXT("-cmd_a=  -not_on_this_list=2 --cmd_b=true "), TEXT("-cmd_a=-not_on_this_list=2 --cmd_b=true") },
		}));
		TCHAR Result[256]{};

		CHECK(FCommandLine::FilterMove(Result, UE_ARRAY_COUNT(Result), Input, AllowedList));
		CHECK(FStringView{ Result } == FStringView{ Expected });
	}

	SECTION("Filtering applies to key values in quotes.. FORT-602120")
	{
		auto [Input, Expected] = GENERATE_COPY(table<const TCHAR*, const TCHAR*>(
			{
				{ TEXT("\"-cmd_a --cmd_b not_on_this_list\""), TEXT("-cmd_a --cmd_b") },
				{ TEXT("\"-cmd_a not_on_this_list --cmd_b\""), TEXT("-cmd_a --cmd_b") },
				{ TEXT("-cmd_a \"not_on_this_list --cmd_b\""), TEXT("-cmd_a --cmd_b") },
			}));
		TCHAR Result[256]{};

		CHECK(FCommandLine::FilterMove(Result, UE_ARRAY_COUNT(Result), Input, AllowedList));
		CHECK(FStringView{ Result } == FStringView{ Expected });
	}


	SECTION("Filtering CLI examples, Using 1 buffer as In and Out")
	{
		auto [Input, Expected] = GENERATE_COPY(table<const TCHAR*, const TCHAR*>(
			{
				{ TEXT(""), TEXT("")},
				{ TEXT("not_on_this_list"), TEXT("") },
				{ TEXT("-cmd_a --cmd_b"), TEXT("-cmd_a --cmd_b") },
				{ TEXT("-cmd_a --cmd_b not_on_this_list"), TEXT("-cmd_a --cmd_b") },
				{ TEXT("-cmd_a not_on_this_list --cmd_b"), TEXT("-cmd_a --cmd_b") },
				{ TEXT("-cmd_a -cmd_a -cmd_a"), TEXT("-cmd_a -cmd_a -cmd_a") },
				{ TEXT("-cmd_a --cmd_b \"-cmd_a --cmd_b not_on_this_list\""), TEXT("-cmd_a --cmd_b -cmd_a --cmd_b") },
				{ TEXT("-cmd_a=1 not_on_this_list=2 --cmd_b=true "), TEXT("-cmd_a=1 --cmd_b=true") },
				{ TEXT("-cmd_a=  not_on_this_list=2 --cmd_b=true "), TEXT("-cmd_a=not_on_this_list=2 --cmd_b=true") },
				{ TEXT("-cmd_a=  -not_on_this_list=2 --cmd_b=true "), TEXT("-cmd_a=-not_on_this_list=2 --cmd_b=true") },
				{ TEXT("\"-cmd_a --cmd_b not_on_this_list\""), TEXT("-cmd_a --cmd_b") },
				{ TEXT("\"-cmd_a not_on_this_list --cmd_b\""), TEXT("-cmd_a --cmd_b") },
				{ TEXT("-cmd_a \"not_on_this_list --cmd_b\""), TEXT("-cmd_a --cmd_b") },
			}));
		TCHAR SourceAndResult[256]{};
		
		FCString::Strcpy(SourceAndResult, Input);
		CHECK(FCommandLine::FilterMove(SourceAndResult, UE_ARRAY_COUNT(SourceAndResult), SourceAndResult, AllowedList));
		CHECK(FStringView{ SourceAndResult } == FStringView{ Expected });
	}

	SECTION("Filtering with an empty AllowedList, returns an empty string")
	{
		const TCHAR* Input = TEXT("-cmd_a --cmd_b");
		TCHAR Result[256] = TEXT("Not Empty");

		CHECK(FCommandLine::FilterMove(Result, UE_ARRAY_COUNT(Result), Input, {}));
		CHECK(Result[0] == TCHAR(0));
	}

	SECTION("Fail for to small a result buffer")
	{
		const TCHAR* Input = TEXT("-cmd_a --cmd_b");
		TCHAR Result[5]{};

		CHECK(false == FCommandLine::FilterMove(Result, UE_ARRAY_COUNT(Result), Input, AllowedList));
		CHECK(Result[0] == TCHAR(0));
	}

	SECTION("End to end as it is currently used")
	{
		TArray<FString> Ignored;
		TArray<FString> ApprovedArgs;
		FCommandLine::Parse(TEXT("-cmd_a --cmd_b /cmd_c"), ApprovedArgs, Ignored);

		auto [Input, Expected] = GENERATE_COPY(table<const TCHAR*, const TCHAR*>(
			{
				{ TEXT(""), TEXT("")},
				{ TEXT("not_on_this_list"), TEXT("") },
				{ TEXT("cmd_a"), TEXT("cmd_a") },
				{ TEXT("-cmd_a"), TEXT("-cmd_a") },
				{ TEXT("--cmd_b"), TEXT("--cmd_b") },
				{ TEXT("/cmd_c"), TEXT("/cmd_c") },

				{ TEXT("cmd_a --cmd_b"), TEXT("cmd_a --cmd_b") },
				{ TEXT("-cmd_a --cmd_b"), TEXT("-cmd_a --cmd_b") },
				{ TEXT("-cmd_a --cmd_b /cmd_c"), TEXT("-cmd_a --cmd_b /cmd_c") },
				{ TEXT("-cmd_a --cmd_b not_on_this_list"), TEXT("-cmd_a --cmd_b") },
				{ TEXT("-cmd_a not_on_this_list --cmd_b"), TEXT("-cmd_a --cmd_b") },
				{ TEXT("cmd_a -cmd_a -cmd_a"), TEXT("cmd_a -cmd_a -cmd_a") },
				{ TEXT("-cmd_a --cmd_b \"-cmd_a --cmd_b not_on_this_list\""), TEXT("-cmd_a --cmd_b -cmd_a --cmd_b") },
				{ TEXT("-cmd_a=1 not_on_this_list=2 --cmd_b=true "), TEXT("-cmd_a=1 --cmd_b=true") },
				{ TEXT("-cmd_a=  not_on_this_list=2 --cmd_b=true "), TEXT("-cmd_a=not_on_this_list=2 --cmd_b=true") },
				{ TEXT("-cmd_a=  -not_on_this_list=2 --cmd_b=true "), TEXT("-cmd_a=-not_on_this_list=2 --cmd_b=true") },
				{ TEXT("\"-cmd_a --cmd_b not_on_this_list\""), TEXT("-cmd_a --cmd_b") },
				{ TEXT("\"-cmd_a not_on_this_list --cmd_b\""), TEXT("-cmd_a --cmd_b") },
				{ TEXT("-cmd_a \"not_on_this_list --cmd_b\""), TEXT("-cmd_a --cmd_b") },
				{ TEXT("-run=../../risky.exe -cmd_a=/mnt/horde/FN+NC+PF/good.bin --cmd_b=c:\\log.txt"), TEXT("-cmd_a=/mnt/horde/FN+NC+PF/good.bin --cmd_b=c:\\log.txt") },
			}));
		TCHAR SourceAndResult[256]{};

		FCString::Strcpy(SourceAndResult, Input);
		CHECK(FCommandLine::FilterMove(SourceAndResult, UE_ARRAY_COUNT(SourceAndResult), SourceAndResult, ApprovedArgs));
		CHECK(FStringView{ SourceAndResult } == FStringView{ Expected });
	}

}
#endif

