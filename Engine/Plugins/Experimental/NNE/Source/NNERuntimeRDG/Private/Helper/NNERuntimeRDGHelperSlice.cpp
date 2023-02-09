// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNERuntimeRDGHelperSlice.h"
#include "NNERuntimeRDGTensorIdxIterator.h"

namespace UE::NNERuntimeRDG::Internal::CPUHelper::Slice
{
	void Apply(const NNECore::Internal::FTensor& InputTensor, NNECore::Internal::FTensor& OutputTensor, TConstArrayView<int32> Starts)
	{
		static constexpr int32 MaxItemInOutputTensor = NNECore::FTensorShape::MaxRank * 2;

		if (OutputTensor.GetVolume() >= MaxItemInOutputTensor)
		{
			return;
		}

		if (OutputTensor.GetDataType() != ENNETensorDataType::Float)
		{
			return;
		}

		if (!InputTensor.HasPreparedData())
		{
			return;
		}

		check(InputTensor.GetShape().Rank() == Starts.Num());
		check(OutputTensor.GetShape().Rank() == Starts.Num());

		TArray<float> OutputData;
		TConstArrayView<float> InputData = InputTensor.GetPreparedData<float>();
		Private::TensorIdxIterator itOutput(OutputTensor.GetShape());
		const Private::TensorIdxIterator itInput(InputTensor.GetShape());

		OutputData.SetNumUninitialized(OutputTensor.GetVolume());
		do
		{
			TArray<uint32> CurInputPosition(itOutput.GetPositions());
			for (int r = 0; r < CurInputPosition.Num(); ++r)
			{
				CurInputPosition[r] += Starts[r];
			}

			float Value = InputData[itInput.GetIndexFromPosition(CurInputPosition)];
			OutputData[itOutput.GetIndex()] = Value;

		} while (itOutput.Advance());

		OutputTensor.SetPreparedData<float>(OutputData);
	}
	
} // UE::NNERuntimeRDG::Private::CPUHelper::Slice
