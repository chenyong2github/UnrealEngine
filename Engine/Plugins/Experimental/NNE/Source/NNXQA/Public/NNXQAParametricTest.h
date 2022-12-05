// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace NNX 
{
namespace Test 
{
	// Run all parametric tests 
	// * NameSubstring will filtering test by full name (NameSubstring should be contained in the name). Empty string to run all tests.
	// * Tag will filter tests by tag. Empty string to run all tests.
	// * RuntimeFilter all to run those test only on the provided runtime. Empty string to run on all runtime.
	NNXQA_API bool RunParametricTests(const FString& NameSubstring, const FString& Tag, const FString& RuntimeFilter);

	//Set the RuntimeFilter witch automation will use, Empty string to run on all runtime (this is default)
	NNXQA_API void SetAutomationRuntimeFilter(const FString& RuntimeFilter);
	
	NNXQA_API bool InitializeParametricTests();
} // namespace Test
} // namespace NNX