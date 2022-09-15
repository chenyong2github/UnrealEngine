// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NNXQAUtils.h"

namespace NNX 
{
namespace Test 
{
	struct FTestsOperatorElementWiseUnary : public FTests
	{
		FTestsOperatorElementWiseUnary();
	private:
		void AddTests(const FString& OpName);
		
		FTests::FTestSetup& AddTest(const FString& OpName, TArrayView<const uint32> Shape, const FString& ExtraSuffix = TEXT(""));
	};

} // namespace Test
} // namespace NNX
