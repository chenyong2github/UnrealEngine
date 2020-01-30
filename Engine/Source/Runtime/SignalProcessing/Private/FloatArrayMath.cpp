// Copyright Epic Games, Inc. All Rights Reserved.

#include "DSP/FloatArrayMath.h"
#include "CoreMinimal.h"

namespace
{
	const float LOGE10 = FMath::Loge(10.f);
}

namespace Audio
{
	void ArrayCumulativeSum(TArrayView<const float> InView, TArray<float>& OutData)
	{
		// Initialize output data
		int32 Num = InView.Num();
		OutData.Reset();
		OutData.AddUninitialized(Num);

		if (Num < 1)
		{
			return;
		}

		float* OutDataPtr = OutData.GetData();
		const float* InViewPtr = InView.GetData();

		// Start summing
		*OutDataPtr = *InViewPtr++;

		for (int32 i = 1; i < Num; i++)
		{
			float Temp = *OutDataPtr++ + *InViewPtr++;
			*OutDataPtr = Temp;
		}
	}

	void ArrayMean(TArrayView<const float> InView, float& OutMean)
	{
		OutMean = 0.f;

		const int32 Num = InView.Num();

		if (Num < 1)
		{
			return;
		}

		const float* DataPtr = InView.GetData();

		for (int32 i = 0; i < Num; i++)
		{
			OutMean += DataPtr[i];
		}

		OutMean /= static_cast<float>(Num);
	}

	void ArrayMeanFilter(TArrayView<const float> InView, int32 WindowSize, int32 WindowOrigin, TArray<float>& OutData)
	{
		// a quick but sinful implementation of a mean filter. encourages floating point rounding errors. 
		check(WindowOrigin < WindowSize);
		check(WindowOrigin >= 0);
		check(WindowSize > 0);

		// Initialize output data
		const int32 Num = InView.Num();
		OutData.Reset();
		OutData.AddUninitialized(Num);

		if (Num < 1)
		{
			return;
		}
		
		// Use cumulative sum to avoid multiple summations 
		// Instead of summing over InView[StartIndex:EndIndex], avoid all that
		// calculation by taking difference of cumulative sum at those two points:
		//  cumsum(X[0:b]) - cumsum(X[0:a]) = sum(X[a:b])
		TArray<float> SummedData;
		ArrayCumulativeSum(InView, SummedData);
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

	void ArrayMaxFilter(TArrayView<const float> InView, int32 WindowSize, int32 WindowOrigin, TArray<float>& OutData)
	{
		// A reasonable implementation of a max filter for the data we're interested in, though surely not the fastest.
		check(WindowOrigin < WindowSize);
		check(WindowOrigin >= 0);
		check(WindowSize > 0);
		
		int32 StartIndex = -WindowOrigin;
		int32 EndIndex = StartIndex + WindowSize;

		// Initialize output
		int32 Num = InView.Num();
		OutData.Reset();
		OutData.AddUninitialized(Num);

		if (Num < 1)
		{
			return;
		}

		// Get max in first window
		int32 ActualStartIndex = 0;
		int32 ActualEndIndex = FMath::Min(EndIndex, Num);

		const float* InViewPtr = InView.GetData();
		float* OutDataPtr = OutData.GetData();
		int32 MaxIndex = 0;
		float MaxValue = InView[0];

		for (int32 i = ActualStartIndex; i < ActualEndIndex; i++)
		{
			if (InViewPtr[i] > MaxValue)
			{
				MaxValue = InViewPtr[i];
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
				MaxValue = InViewPtr[MaxIndex];
				for (int32 j = ActualStartIndex + 1; j < ActualEndIndex; j++)
				{
					if (InViewPtr[j] > MaxValue)
					{
						MaxIndex = j;
						MaxValue = InViewPtr[MaxIndex];
					}
				}
			}
			else
			{
				// We only need to inspect the newest sample because the previous maximum value was in this window.
				if (InViewPtr[ActualEndIndex - 1] > MaxValue)
				{
					MaxIndex = ActualEndIndex - 1;
					MaxValue = InViewPtr[MaxIndex];
				}
			}

			OutDataPtr[i] = MaxValue;

			StartIndex++;
			EndIndex++;
		}
	}

	void ArrayGetEuclideanNorm(TArrayView<const float> InView, float& OutEuclideanNorm)
	{
		// Initialize output.
		OutEuclideanNorm = 0.0f;
		const int32 Num = InView.Num();
		const float* InViewData = InView.GetData();
		
		// Sum it up.
		for (int32 i = 0; i < Num; i++)
		{
			OutEuclideanNorm += InViewData[i] * InViewData[i];
		}

		OutEuclideanNorm = FMath::Sqrt(OutEuclideanNorm);
	}

	void ArrayAbsInPlace(TArrayView<float> InView)
	{
		const int32 Num = InView.Num();
		float* Data = InView.GetData();

		for (int32 i = 0; i < Num; i++)
		{
			Data[i] = FMath::Abs(Data[i]);
		}
	}

	void ArrayClampMinInPlace(TArrayView<float> InView, float InMin)
	{
		const int32 Num = InView.Num();
		float* Data = InView.GetData();

		for (int32 i = 0; i < Num; i++)
		{
			Data[i] = FMath::Max(InMin, Data[i]);
		}
	}

	void ArrayClampMaxInPlace(TArrayView<float> InView, float InMax)
	{
		const int32 Num = InView.Num();
		float* Data = InView.GetData();

		for (int32 i = 0; i < Num; i++)
		{
			Data[i] = FMath::Min(InMax, Data[i]);
		}
	}

	void ArrayClampInPlace(TArrayView<float> InView, float InMin, float InMax)
	{
		const int32 Num = InView.Num();
		float* Data = InView.GetData();

		for (int32 i = 0; i < Num; i++)
		{
			Data[i] = FMath::Clamp(Data[i], InMin, InMax);
		}
	}

	void ArrayMinMaxNormalize(TArrayView<const float> InView, TArray<float>& OutArray)
	{
		const int32 Num = InView.Num();
		OutArray.Reset(Num);

		if (Num < 1)
		{
			return;
		}

		OutArray.AddUninitialized(Num);

		const float* InDataPtr = InView.GetData();
		float MaxValue = InDataPtr[0];
		float MinValue = InDataPtr[0];

		// determine min and max
		for (int32 i = 1; i < Num; i++)
		{
			if (InDataPtr[i] < MinValue)
			{
				MinValue = InDataPtr[i];
			}
			else if (InDataPtr[i] > MaxValue)
			{
				MaxValue = InDataPtr[i];
			}
		}

		// Normalize data by subtracting minimum value and dividing by range
		float* OutDataPtr = OutArray.GetData();
		float Scale = 1.f / FMath::Max(SMALL_NUMBER, MaxValue - MinValue);
		for (int32 i = 0; i < Num; i++)
		{
			OutDataPtr[i] = (InDataPtr[i] - MinValue) * Scale;
		}
	}

	void ArrayMultiplyByConstantInPlace(TArrayView<float> InView, float InMultiplier)
	{
		const int32 Num = InView.Num();
		float* InViewData = InView.GetData();
		for (int32 i = 0; i < Num; i++)
		{
			InViewData[i] *= InMultiplier;
		}
	}

	void ArraySubtractByConstantInPlace(TArrayView<float> InView, float InSubtrahend)
	{
		const int32 Num = InView.Num();
		float* InViewData = InView.GetData();
		for (int32 i = 0; i < Num; i++)
		{
			InViewData[i] -= InSubtrahend;
		}
	}

	void ArraySubtract(TArrayView<const float> InMinuend, TArrayView<const float> InSubtrahend, TArray<float>& OutArray)
	{
		const int32 Num = InMinuend.Num();

		checkf(Num == InSubtrahend.Num(), TEXT("InMinuend and InSubtrahend must have equal Num elements (%d vs %d)"), Num, InSubtrahend.Num());

		OutArray.Reset(Num);

		if (Num < 1)
		{
			return;
		}

		OutArray.AddUninitialized(Num);
		
		const float* MinuendPtr = InMinuend.GetData();
		const float* SubtrahendPtr = InSubtrahend.GetData();
		float* OutPtr = OutArray.GetData();

		for (int32 i = 0; i < Num; i++)
		{
			OutPtr[i] = MinuendPtr[i] - SubtrahendPtr[i];
		}
	}

	void ArrayMagnitudeToDecibelInPlace(TArrayView<float> InView)
	{
		const int32 Num = InView.Num();
		float* InViewData = InView.GetData();
		for (int32 i = 0; i < Num; i++)
		{
			InViewData[i] = 20.f * FMath::Loge(InViewData[i]) / LOGE10;
		}
	}

	void ArrayPowerToDecibelInPlace(TArrayView<float> InView)
	{
		const int32 Num = InView.Num();
		float* InViewData = InView.GetData();
		for (int32 i = 0; i < Num; i++)
		{
			InViewData[i] = 10.f * FMath::Loge(InViewData[i]) / LOGE10;
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

	void FContiguousSparse2DKernelTransform::TransformArray(TArrayView<const float> InView, TArray<float>& OutArray) const
	{
		check(InView.Num() == NumIn);

		// Resize output
		OutArray.Reset(NumOut);
		if (NumOut > 0)
		{
			OutArray.AddUninitialized(NumOut);
		}

		TransformArray(InView.GetData(), OutArray.GetData());
	}

	void FContiguousSparse2DKernelTransform::TransformArray(TArrayView<const float> InView, AlignedFloatBuffer& OutArray) const
	{	
		check(InView.Num() == NumIn);

		// Resize output
		OutArray.Reset(NumOut);
		if (NumOut > 0)
		{
			OutArray.AddUninitialized(NumOut);
		}

		TransformArray(InView.GetData(), OutArray.GetData());
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
