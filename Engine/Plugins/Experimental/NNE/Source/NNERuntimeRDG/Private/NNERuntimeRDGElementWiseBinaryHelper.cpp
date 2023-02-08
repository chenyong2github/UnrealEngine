// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNERuntimeRDGElementWiseBinaryHelper.h"
#include "NNECoreTypes.h"
#include "NNECoreTensor.h"
#include "Math/UnrealMathUtility.h"
#include "MathUtil.h"

namespace UE::NNERuntimeRDG::Internal::ElementWiseBinaryCPUHelper
{
	class TensorIdxIterator
	{
		const NNECore::FTensorShape& TensorShape;
		TArray<uint32, TInlineAllocator<NNECore::FTensorShape::MaxRank>> CurrentPosition;

	public:
		TensorIdxIterator(const NNECore::FTensorShape& InTensorShape);
		bool Advance();
		int32 GetIndexToBroadcastedShape(const NNECore::FTensorShape& InTensorShape) const;
	};
	
	TensorIdxIterator::TensorIdxIterator(const NNECore::FTensorShape& InTensorShape) : TensorShape(InTensorShape)
	{
		CurrentPosition.Init(0, InTensorShape.Rank());
	}

	bool TensorIdxIterator::Advance()
	{
		for (int32 i = TensorShape.Rank() - 1; i >= 0; --i)
		{
			++CurrentPosition[i];
			if (CurrentPosition[i] < TensorShape.GetData()[i])
			{
				return true;
			}
			CurrentPosition[i] = 0;
		}
		return false;
	}

	int32 TensorIdxIterator::GetIndexToBroadcastedShape(const NNECore::FTensorShape& InTensorShape) const
	{
		int32 Index = 0;
		int32 DimBaseOffset = 1;
		for (int32 r = TensorShape.Rank() - 1; r >= 0; --r)
		{
			if (r >= InTensorShape.Rank())
			{
				break;
			}
			Index += FMath::Min(CurrentPosition[r], InTensorShape.GetData()[r]-1) * DimBaseOffset;
			DimBaseOffset *= FMath::Min(TensorShape.GetData()[r], InTensorShape.GetData()[r]);
		}
		return Index;
	}

	template<NNECore::Internal::EElementWiseBinaryOperatorType OpType> float Apply(float X, float Y);
	template<> float Apply<NNECore::Internal::EElementWiseBinaryOperatorType::Add>(float X, float Y) { return X + Y; }
	template<> float Apply<NNECore::Internal::EElementWiseBinaryOperatorType::Div>(float X, float Y) { return X / Y; }
	template<> float Apply<NNECore::Internal::EElementWiseBinaryOperatorType::Mod>(float X, float Y) { return FMath::Fmod(X, Y); }
	template<> float Apply<NNECore::Internal::EElementWiseBinaryOperatorType::Mul>(float X, float Y) { return X * Y; }
	template<> float Apply<NNECore::Internal::EElementWiseBinaryOperatorType::Prelu>(float X, float Y) { return (X < 0.0f) ? (Y * X) : X; }
	template<> float Apply<NNECore::Internal::EElementWiseBinaryOperatorType::Pow>(float X, float Y) { return FMath::Pow(X, Y); }
	template<> float Apply<NNECore::Internal::EElementWiseBinaryOperatorType::Sub>(float X, float Y) { return X - Y; }

	template<NNECore::Internal::EElementWiseBinaryOperatorType OpType> void Apply(const NNECore::Internal::FTensor& LHSTensor, const NNECore::Internal::FTensor& RHSTensor, NNECore::Internal::FTensor& OutputTensor)
	{
		//Heuristic to avoid unexpected performance hit. This helper being intended for shape related arithmetic only.
		static constexpr int32 MaxItemInInputTensors = NNECore::FTensorShape::MaxRank * 2;

		if (LHSTensor.HasPreparedData() && 
			RHSTensor.HasPreparedData() && 
			(LHSTensor.GetVolume() <= MaxItemInInputTensors) &&
			(RHSTensor.GetVolume() <= MaxItemInInputTensors))
		{
			TConstArrayView<float> LHSData = LHSTensor.GetPreparedData<float>();
			TConstArrayView<float> RHSData = RHSTensor.GetPreparedData<float>();
			TArray<float> OutputData;
			OutputData.Reserve(OutputTensor.GetVolume());

			TensorIdxIterator it(OutputTensor.GetShape());
			do
			{
				int32 LHSIdx = it.GetIndexToBroadcastedShape(LHSTensor.GetShape());
				int32 RHSIdx = it.GetIndexToBroadcastedShape(RHSTensor.GetShape());
				float LHSValue = LHSData.GetData()[LHSIdx];
				float RHSValue = RHSData.GetData()[RHSIdx];
				OutputData.Add(Apply<OpType>(LHSValue, RHSValue));
			} while (it.Advance());

			check(OutputData.Num() == OutputTensor.GetVolume());
			OutputTensor.SetPreparedData<float>(OutputData);
		}
	}

	void Apply(NNECore::Internal::EElementWiseBinaryOperatorType OpType, const NNECore::Internal::FTensor& LHSTensor, const NNECore::Internal::FTensor& RHSTensor, NNECore::Internal::FTensor& OutputTensor)
	{
		switch (OpType)
		{
		case NNECore::Internal::EElementWiseBinaryOperatorType::Add:
			Apply<NNECore::Internal::EElementWiseBinaryOperatorType::Add>(LHSTensor, RHSTensor, OutputTensor);
			break;
		case NNECore::Internal::EElementWiseBinaryOperatorType::Div:
			Apply<NNECore::Internal::EElementWiseBinaryOperatorType::Div>(LHSTensor, RHSTensor, OutputTensor);
			break;
		case NNECore::Internal::EElementWiseBinaryOperatorType::Mod:
			Apply<NNECore::Internal::EElementWiseBinaryOperatorType::Mod>(LHSTensor, RHSTensor, OutputTensor);
			break;
		case NNECore::Internal::EElementWiseBinaryOperatorType::Mul:
			Apply<NNECore::Internal::EElementWiseBinaryOperatorType::Mul>(LHSTensor, RHSTensor, OutputTensor);
			break;
		case NNECore::Internal::EElementWiseBinaryOperatorType::Prelu:
			Apply<NNECore::Internal::EElementWiseBinaryOperatorType::Prelu>(LHSTensor, RHSTensor, OutputTensor);
			break;
		case NNECore::Internal::EElementWiseBinaryOperatorType::Pow:
			Apply<NNECore::Internal::EElementWiseBinaryOperatorType::Pow>(LHSTensor, RHSTensor, OutputTensor);
			break;
		case NNECore::Internal::EElementWiseBinaryOperatorType::Sub:
			Apply<NNECore::Internal::EElementWiseBinaryOperatorType::Sub>(LHSTensor, RHSTensor, OutputTensor);
			break;
		default:
			break;
		}
	}
	
} // UE::NNERuntimeRDG::Internal::ElementWiseUnaryCPUHelper
