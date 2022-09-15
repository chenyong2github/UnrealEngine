// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace NNX 
{
namespace Test 
{
	// Test inference on a model across all runtime
	NNXQA_API bool TestModelFromName(const FString& ModelName);

	// Test inference on a model across all runtime
	NNXQA_API bool TestModelFromPath(const FString& ModelPath);
	
	// Test inference on all test models across all runtime
	NNXQA_API bool TestAllModels();

} // namespace Test
} // namespace NNX