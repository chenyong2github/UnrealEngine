// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ArrayView.h"
#include "Containers/UnrealString.h"
#include "NNXRuntime.h"

namespace NNX 
{
namespace Test 
{
	struct FTests
	{
		struct FTestSetup
		{
			FTestSetup(const FString& ModelOrOperatorName, const FString& TestSuffix) :
				TestName(ModelOrOperatorName+TestSuffix),
				TargetName(ModelOrOperatorName),
				AbsoluteErrorEpsilon(1e-5f),
				RelativeErrorPercent(1e-3f) {}

			const FString TestName;
			const FString TargetName;
			float AbsoluteErrorEpsilon;
			float RelativeErrorPercent;
			TMap<FString, float> AbsoluteErrorEpsilonForRuntime;
			TMap<FString, float> RelativeErrorPercentForRuntime;
			TArray<FMLTensorDesc> Inputs;
			TArray<FMLTensorDesc> Outputs;
			TArray<FString> AutomationExcludedRuntime;
			TArray<FString> AutomationExcludedPlatform;
			TArray<TPair<FString, FString>> AutomationExcludedPlatformRuntimeCombination;

			float GetAbsoluteErrorEpsilonForRuntime(const FString& RuntimeName) const
			{
				const float* SpecializedValue = AbsoluteErrorEpsilonForRuntime.Find(RuntimeName);
				return (SpecializedValue != nullptr) ? *SpecializedValue : AbsoluteErrorEpsilon;

			}
			float GetRelativeErrorPercentForRuntime(const FString& RuntimeName) const
			{
				const float* SpecializedValue = RelativeErrorPercentForRuntime.Find(RuntimeName);
				return (SpecializedValue != nullptr) ? *SpecializedValue : RelativeErrorPercent;
			}
		};


		FTestSetup& AddTest(const FString& ModelOrOperatorName, const FString& TestSuffix);

		TArray<FTestSetup> TestSetups;
	};

	bool CompareONNXModelInferenceAcrossRuntimes(const FString& Name, TArrayView<uint8> ModelData, const FTests::FTestSetup* AutomationTestSetup);

	FString FMLTensorDescToString(const FMLTensorDesc& TensorDesc);
	FString TensorToString(const FMLTensorDesc& TensorDesc, const TArray<char>& TensorData);
	FString ShapeToString(TArrayView<const uint32> Shape);

} // namespace Test
} // namespace NNX