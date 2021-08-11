// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralNetworkInferenceBackEndUtils.h"
#include "HAL/PlatformTime.h"



/* FNeuralNetworkInferenceBackEndUtils public functions
 *****************************************************************************/

bool FNeuralNetworkInferenceBackEndUtils::SizeSanityChecks(const TArray<FNeuralTensor*>& InTensorArray, const int32 InMinNum, const int32 InMaxNum,
	const int32 InMinDimensions, const int32 InMaxDimensions, const int32 InDimensionRangeFirst, const int32 InDimensionRangeLast)
{
	// Do InMinNum/InMaxNum checks
	if (InTensorArray.Num() < InMinNum || InTensorArray.Num() > InMaxNum)
	{
		UE_LOG(LogNeuralNetworkInferenceBackEnd, Warning,
			TEXT("FNeuralNetworkInferenceBackEndUtils::SizeSanityChecks(): %d <= InputTensors.Num() (%d) <= %d failed."), InMinNum, InTensorArray.Num(), InMaxNum);
		return false;
	}
	// Do Tensor != nullptr checks
	for (const FNeuralTensor* const Tensor : InTensorArray)
	{
		if (!Tensor)
		{
			UE_LOG(LogNeuralNetworkInferenceBackEnd, Warning,
				TEXT("FNeuralNetworkInferenceBackEndUtils::SizeSanityChecks(): InTensorArray contained nullptr elements."));
			return false;
		}
	}
	// Do InMinDimensions/InMaxDimensions checks (if enabled)
	if (InMinDimensions > -1 || InDimensionRangeLast > -1)
	{
		const int32 DimensionRangeFirst = (InDimensionRangeFirst > -1 ? InDimensionRangeFirst : 0);
		const int32 DimensionRangeLast = FMath::Min(InTensorArray.Num(), // Make sure it does not go out of indexes (i.e., InMinNum != InMaxNum and InTensorArray < InMaxNum)
			InDimensionRangeLast > -1 ? InDimensionRangeLast + 1 : InTensorArray.Num()); // From 1-index to 0-index
		for (int32 TensorIndex = DimensionRangeFirst; TensorIndex < DimensionRangeLast; ++TensorIndex)
		{
			const FNeuralTensor* const Tensor = InTensorArray[TensorIndex];
			if (InMinDimensions > -1 && Tensor->GetNumberDimensions() < InMinDimensions)
			{
				UE_LOG(LogNeuralNetworkInferenceBackEnd, Warning,
					TEXT("FNeuralNetworkInferenceBackEndUtils::SizeSanityChecks(): %d <= Tensors[%d]->GetNumberDimensions() (%d) failed."),
					InMinDimensions, TensorIndex, Tensor->GetNumberDimensions());
				return false;
			}
			else if (InMaxDimensions > -1 && Tensor->GetNumberDimensions() > InMaxDimensions)
			{
				UE_LOG(LogNeuralNetworkInferenceBackEnd, Warning,
					TEXT("FNeuralNetworkInferenceBackEndUtils::SizeSanityChecks(): Tensors[%d]->GetNumberDimensions() (%d) <= %d failed."),
					TensorIndex, Tensor->GetNumberDimensions(), InMaxDimensions);
				return false;
			}
		}
	}
	return true;
}
