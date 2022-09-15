// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NNXQAUtils.h"
#include <initializer_list>

namespace NNX 
{
namespace Test 
{
	struct FTestsOperatorElementWiseVariadic : public FTests
	{
		FTestsOperatorElementWiseVariadic();
	private:
		void AddTests(const FString& OpName);

		FTests::FTestSetup& AddTest(const FString& OpName, std::initializer_list<std::initializer_list<const uint32>> ShapeInputs, TArrayView<const uint32> ShapeOut);
	};

} // namespace Test
} // namespace NNX
