// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace NNX 
{
namespace Test 
{
	// Run a unit tests for an operator across all runtime
	NNXQA_API bool RunOperatorTest(const FString& TestName);
	
	// Run all unit tests for operators across all runtime
	NNXQA_API bool RunAllOperatorTests();

} // namespace Test
} // namespace NNX