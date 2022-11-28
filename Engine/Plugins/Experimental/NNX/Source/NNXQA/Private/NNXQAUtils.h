// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "NNXRuntime.h"
#include "NNECoreAttributeMap.h"

struct FNNIModelRaw;

namespace NNX 
{
namespace Test 
{
	struct FTests
	{
		typedef TArray<char> FTensorData;
		
		struct FTestSetup
		{
			static constexpr float DefaultAbsoluteErrorEpsilon = 1e-5f;
			static constexpr float DefaultRelativeErrorPercent = 1e-3f;

			FTestSetup(const FString& InTestCategory, const FString& InModelOrOperatorName, const FString& InTestSuffix) :
				TestName(InTestCategory + InModelOrOperatorName + InTestSuffix),
				TargetName(InModelOrOperatorName),
				AbsoluteErrorEpsilon(DefaultAbsoluteErrorEpsilon),
				RelativeErrorPercent(DefaultRelativeErrorPercent),
				IsModelTest(false)
			{}
			
			const FString TestName;
			const FString TargetName;
			float AbsoluteErrorEpsilon;
			float RelativeErrorPercent;
			bool IsModelTest;
			TMap<FString, float> AbsoluteErrorEpsilonForRuntime;
			TMap<FString, float> RelativeErrorPercentForRuntime;
			TArray<FTensor> Inputs;
			TArray<FTensor> Weights;
			TArray<FTensor> Outputs;
			TArray<FTensorData> InputsData;
			TArray<FTensorData> WeightsData;
			TArray<FTensorData> OutputsData;
			UE::NNECore::FAttributeMap AttributeMap;
			TArray<FString> Tags;
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


		FTestSetup& AddTest(const FString& Category, const FString& ModelOrOperatorName, const FString& TestSuffix);

		TArray<FTestSetup> TestSetups;
	};

	bool CompareONNXModelInferenceAcrossRuntimes(const FNNIModelRaw& ONNXModel, const FNNIModelRaw& ONNXModelVariadic, const FTests::FTestSetup& TestSetup, const FString& RuntimeFilter = TEXT(""));

	FString TensorToString(const FTensor& Tensor);
	FString TensorToString(const FTensor& Tensor, TConstArrayView<char> TensorData);
	template<typename T> FString ShapeToString(TConstArrayView<T> Shape);
	TArray<char> GenerateTensorDataForTest(const FTensor& Tensor, int TensorIndex);

} // namespace Test
} // namespace NNX