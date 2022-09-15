// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NNXQAUtils.h"

namespace NNX 
{
namespace Test 
{
	struct FTestsOperatorElementWiseBinary : public FTests
	{
		FTestsOperatorElementWiseBinary();
	private:
		void AddTests(const FString& OpName);
		
		FTestSetup& AddTest(const FString& OpName, TArrayView<const uint32> ShapeLHS, TArrayView<const uint32> ShapeRHS, TArrayView<const uint32> ShapeOut);
	};

} // namespace Test
} // namespace NNX
