// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include <functional>
#include "NNECoreAttributeMap.h"
#include "NNXRuntime.h"
#include "NNXRuntimeFormat.h"
#include "NNECoreTypes.h"
#include "NNECoreTensor.h"


namespace NNX 
{

namespace Test 
{
	struct FTests
	{
		typedef TArray<char> FTensorData;
		
		struct FTestSetup
		{
			/* defaults from https://numpy.org/doc/stable/reference/generated/numpy.isclose.html */
			static constexpr float DefaultAbsoluteTolerance = 1e-8f;
			static constexpr float DefaultRelativeTolerance = 1e-5f;

			FTestSetup(const FString& InTestCategory, const FString& InModelOrOperatorName, const FString& InTestSuffix) :
				TestName(InTestCategory + InModelOrOperatorName + InTestSuffix),
				TargetName(InModelOrOperatorName),
				AbsoluteTolerance(DefaultAbsoluteTolerance),
				RelativeTolerance(DefaultRelativeTolerance),
				IsModelTest(false)
			{}
			
			const FString TestName;
			const FString TargetName;
			float AbsoluteTolerance;
			float RelativeTolerance;
			bool IsModelTest;
			TMap<FString, float> AbsoluteToleranceForRuntime;
			TMap<FString, float> RelativeToleranceForRuntime;
			TArray<UE::NNECore::Internal::FTensor> Inputs;
			TArray<UE::NNECore::Internal::FTensor> Weights;
			TArray<UE::NNECore::Internal::FTensor> Outputs;
			TArray<FTensorData> InputsData;
			TArray<FTensorData> WeightsData;
			TArray<FTensorData> OutputsData;
			UE::NNECore::FAttributeMap AttributeMap;
			TArray<FString> Tags;
			TArray<FString> AutomationExcludedRuntime;
			TArray<FString> AutomationExcludedPlatform;
			TArray<TPair<FString, FString>> AutomationExcludedPlatformRuntimeCombination;

			float GetAbsoluteToleranceForRuntime(const FString& RuntimeName) const
			{
				const float* SpecializedValue = AbsoluteToleranceForRuntime.Find(RuntimeName);
				return (SpecializedValue != nullptr) ? *SpecializedValue : AbsoluteTolerance;

			}
			float GetRelativeToleranceForRuntime(const FString& RuntimeName) const
			{
				const float* SpecializedValue = RelativeToleranceForRuntime.Find(RuntimeName);
				return (SpecializedValue != nullptr) ? *SpecializedValue : RelativeTolerance;
			}
		};


		FTestSetup& AddTest(const FString& Category, const FString& ModelOrOperatorName, const FString& TestSuffix);

		TArray<FTestSetup> TestSetups;
	};

	bool CompareONNXModelInferenceAcrossRuntimes(const FNNIModelRaw& ONNXModel, const FNNIModelRaw& ONNXModelVariadic, 
		const FTests::FTestSetup& TestSetup, const FString& RuntimeFilter = TEXT(""));
	
	class ElementWiseCosTensorInitializer
	{
		ENNETensorDataType DataType;
		uint32 TensorIndex;

	public:
		ElementWiseCosTensorInitializer(ENNETensorDataType InDataType, uint32 InTensorIndex);
		float operator () (uint32 ElementIndex) const;
	};

	FString TensorToString(const UE::NNECore::Internal::FTensor& Tensor);
	FString TensorToString(const UE::NNECore::Internal::FTensor& Tensor, TConstArrayView<char> TensorData);
	template<typename T> FString ShapeToString(TConstArrayView<T> Shape);
	TArray<char> GenerateTensorDataForTest(const UE::NNECore::Internal::FTensor& Tensor, std::function<float(uint32)> ElementInitializer);

} // namespace Test
} // namespace NNX