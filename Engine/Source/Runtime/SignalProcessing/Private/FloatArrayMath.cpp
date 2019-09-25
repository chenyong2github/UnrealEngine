// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DSP/FloatArrayMath.h"
#include "CoreMinimal.h"

namespace
{
	const float LOGE10 = FMath::Loge(10.f);
}

namespace Audio
{
	void ArrayCumulativeSum(const TArray<float>& InData, TArray<float>& OutData)
	{
		// Initialize output data
		int32 Num = InData.Num();
		OutData.Reset();
		OutData.AddUninitialized(Num);

		if (Num < 1)
		{
			return;
		}

		float* OutDataPtr = OutData.GetData();
		const float* InDataPtr = InData.GetData();

		// Start summing
		*OutDataPtr = *InDataPtr++;

		for (int32 i = 1; i < Num; i++)
		{
			float Temp = *OutDataPtr++ + *InDataPtr++;
			*OutDataPtr = Temp;
		}
	}

	void ArrayMeanFilter(const TArray<float>& InData, int32 WindowSize, int32 WindowOrigin, TArray<float>& OutData)
	{
		// a quick but sinful implementation of a mean filter. encourages floating point rounding errors. 
		check(WindowOrigin < WindowSize);
		check(WindowOrigin >= 0);
		check(WindowSize > 0);

		// Initialize output data
		const int32 Num = InData.Num();
		OutData.Reset();
		OutData.AddUninitialized(Num);

		if (Num < 1)
		{
			return;
		}
		
		// Use cumulative sum to avoid multiple summations 
		// Instead of summing over InData[StartIndex:EndIndex], avoid all that
		// calculation by taking difference of cumulative sum at those two points:
		//  cumsum(X[0:b]) - cumsum(X[0:a]) = sum(X[a:b])
		TArray<float> SummedData;
		ArrayCumulativeSum(InData, SummedData);
		const float LastSummedData = SummedData.Last();
		
		
		const int32 LastIndexBeforeEndBoundaryCondition = FMath::Max(WindowOrigin + 1, Num - WindowSize + WindowOrigin + 1);
		const int32 StartOffset = -WindowOrigin - 1;
		const int32 EndOffset = WindowSize - WindowOrigin - 1;
		const int32 WindowTail = WindowSize - WindowOrigin;

		float* OutDataPtr = OutData.GetData();
		const float* SummedDataPtr = SummedData.GetData();

		if ((WindowSize - WindowOrigin) < Num)
		{
			// Handle boundary condition where analysis window precedes beginning of array.
			for (int32 i = 0; i < (WindowOrigin + 1); i++)
			{
				OutDataPtr[i] = SummedDataPtr[i + EndOffset] / FMath::Max(1.f, static_cast<float>(WindowTail + i));
			}

			// No boundary conditions to handle here.	
			const float MeanDivisor = static_cast<float>(WindowSize);
			for (int32 i = WindowOrigin + 1; i < LastIndexBeforeEndBoundaryCondition; i++)
			{
				OutDataPtr[i] = (SummedDataPtr[i + EndOffset] - SummedDataPtr[i + StartOffset]) / MeanDivisor;
			}
		}
		else
		{
			// Handle boundary condition where window precedes beginning and goes past end of array
			const float ArrayMean = LastSummedData / static_cast<float>(Num);
			for (int32 i = 0; i < LastIndexBeforeEndBoundaryCondition; i++)
			{
				OutDataPtr[i] = ArrayMean;
			}
		}

		// Handle boundary condition where analysis window goes past end of array.
		for (int32 i = LastIndexBeforeEndBoundaryCondition; i < Num; i++)
		{
			OutDataPtr[i] = (LastSummedData - SummedDataPtr[i + StartOffset]) / static_cast<float>(Num - i + WindowOrigin);
		}
	}

	void ArrayMaxFilter(const TArray<float>& InData, int32 WindowSize, int32 WindowOrigin, TArray<float>& OutData)
	{
		// A reasonable implementation of a max filter for the data we're interested in, though surely not the fastest.
		check(WindowOrigin < WindowSize);
		check(WindowOrigin >= 0);
		check(WindowSize > 0);
		
		int32 StartIndex = -WindowOrigin;
		int32 EndIndex = StartIndex + WindowSize;

		// Initialize output
		int32 Num = InData.Num();
		OutData.Reset();
		OutData.AddUninitialized(Num);

		if (Num < 1)
		{
			return;
		}

		// Get max in first window
		int32 ActualStartIndex = 0;
		int32 ActualEndIndex = FMath::Min(EndIndex, Num);

		const float* InDataPtr = InData.GetData();
		float* OutDataPtr = OutData.GetData();
		int32 MaxIndex = 0;
		float MaxValue = InData[0];

		for (int32 i = ActualStartIndex; i < ActualEndIndex; i++)
		{
			if (InDataPtr[i] > MaxValue)
			{
				MaxValue = InDataPtr[i];
				MaxIndex = i;
			}		
		}
		OutDataPtr[0] = MaxValue;

		StartIndex++;
		EndIndex++;

		// Get max in remaining windows
		for (int32 i = 1; i < Num; i++)
		{
			ActualStartIndex = FMath::Max(StartIndex, 0);
			ActualEndIndex = FMath::Min(EndIndex, Num);

			if (MaxIndex < StartIndex)
			{
				// We need to evaluate the entire window because the previous maximum value was not in this window.
				MaxIndex = ActualStartIndex;
				MaxValue = InDataPtr[MaxIndex];
				for (int32 j = ActualStartIndex + 1; j < ActualEndIndex; j++)
				{
					if (InDataPtr[j] > MaxValue)
					{
						MaxIndex = j;
						MaxValue = InDataPtr[MaxIndex];
					}
				}
			}
			else
			{
				// We only need to inspect the newest sample because the previous maximum value was in this window.
				if (InDataPtr[ActualEndIndex - 1] > MaxValue)
				{
					MaxIndex = ActualEndIndex - 1;
					MaxValue = InDataPtr[MaxIndex];
				}
			}

			OutDataPtr[i] = MaxValue;

			StartIndex++;
			EndIndex++;
		}
	}

	void ArrayGetEuclideanNorm(const TArray<float>& InArray, float& OutEuclideanNorm)
	{
		// Initialize output.
		OutEuclideanNorm = 0.0f;
		const int32 Num = InArray.Num();
		const float* InArrayData = InArray.GetData();
		
		// Sum it up.
		for (int32 i = 0; i < Num; i++)
		{
			OutEuclideanNorm += InArrayData[i] * InArrayData[i];
		}

		OutEuclideanNorm = FMath::Sqrt(OutEuclideanNorm);
	}

	void ArrayMultiplyByConstantInPlace(TArray<float>& InArray, float InMultiplier)
	{
		const int32 Num = InArray.Num();
		float* InArrayData = InArray.GetData();
		for (int32 i = 0; i < Num; i++)
		{
			InArrayData[i] *= InMultiplier;
		}
	}

	void ArraySubtractByConstantInPlace(TArray<float>& InArray, float InSubtrahend)
	{
		const int32 Num = InArray.Num();
		float* InArrayData = InArray.GetData();
		for (int32 i = 0; i < Num; i++)
		{
			InArrayData[i] -= InSubtrahend;
		}
	}

	void ArrayMagnitudeToDecibelInPlace(TArray<float>& InArray)
	{
		const int32 Num = InArray.Num();
		float* InArrayData = InArray.GetData();
		for (int32 i = 0; i < Num; i++)
		{
			InArrayData[i] = 20.f * FMath::Loge(InArrayData[i]) / LOGE10;
		}
	}

	void ArrayPowerToDecibelInPlace(TArray<float>& InArray)
	{
		const int32 Num = InArray.Num();
		float* InArrayData = InArray.GetData();
		for (int32 i = 0; i < Num; i++)
		{
			InArrayData[i] = 10.f * FMath::Loge(InArrayData[i]) / LOGE10;
		}
	}

	FContiguousSparse2DKernelTransform::FContiguousSparse2DKernelTransform(const int32 NumInElements, const int32 NumOutElements)
	:	NumIn(NumInElements)
	,	NumOut(NumOutElements)
	{
		check(NumIn >= 0);
		check(NumOut >= 0)
		FRow EmptyRow;
		EmptyRow.StartIndex = 0;

		// Fill up the kernel with emptp rows
		Kernel.Init(EmptyRow, NumOut);
	}

	int32 FContiguousSparse2DKernelTransform::GetNumInElements() const
	{
		return NumIn;
	}


	int32 FContiguousSparse2DKernelTransform::GetNumOutElements() const
	{
		return NumOut;
	}

	void FContiguousSparse2DKernelTransform::SetRow(const int32 RowIndex, const int32 StartIndex, TArrayView<const float> OffsetValues)
	{
		check((StartIndex + OffsetValues.Num()) <= NumIn);

		// Copy row data internally
		Kernel[RowIndex].StartIndex = StartIndex;
		Kernel[RowIndex].OffsetValues = TArray<float>(OffsetValues.GetData(), OffsetValues.Num());
	}

	void FContiguousSparse2DKernelTransform::TransformArray(TArrayView<const float> InArray, TArray<float>& OutArray) const
	{
		check(InArray.Num() == NumIn);

		// Resize output
		OutArray.Reset(NumOut);
		if (NumOut > 0)
		{
			OutArray.AddUninitialized(NumOut);
		}

		TransformArray(InArray.GetData(), OutArray.GetData());
	}

	void FContiguousSparse2DKernelTransform::TransformArray(TArrayView<const float> InArray, AlignedFloatBuffer& OutArray) const
	{	
		check(InArray.Num() == NumIn);

		// Resize output
		OutArray.Reset(NumOut);
		if (NumOut > 0)
		{
			OutArray.AddUninitialized(NumOut);
		}

		TransformArray(InArray.GetData(), OutArray.GetData());
	}

	void FContiguousSparse2DKernelTransform::TransformArray(const float* InArray, float* OutArray) const
	{
		check(nullptr != InArray);
		check(nullptr != OutArray);

		// Initialize output
		FMemory::Memset(OutArray, 0, sizeof(float) * NumOut);

		// Apply kernel one row at a time
		const FRow* KernelData = Kernel.GetData();
		for (int32 RowIndex = 0; RowIndex < Kernel.Num(); RowIndex++)
		{
			const FRow& Row = KernelData[RowIndex];

			// Get offset pointer into input array.
			const float* OffsetInData = &InArray[Row.StartIndex];
			// Get offset pointer of row.
			const float* RowValuePtr = Row.OffsetValues.GetData();

			// dot prod 'em. 
			int32 NumToMult = Row.OffsetValues.Num();
			for (int32 i = 0; i < NumToMult; i++)
			{
				OutArray[RowIndex] += OffsetInData[i] * RowValuePtr[i];
			}
		}
	}
}
