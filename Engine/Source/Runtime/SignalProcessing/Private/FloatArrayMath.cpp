// Copyright Epic Games, Inc. All Rights Reserved.

#include "DSP/FloatArrayMath.h"
#include "CoreMinimal.h"

namespace Audio
{
	namespace MathIntrinsics
	{
		const float Loge10 = FMath::Loge(10.f);
		const int32 SimdMask = 0xFFFFFFFC;
		const int32 NotSimdMask = 0x00000003;
	}

	void ArraySum(TArrayView<const float> InValues, float& OutSum)
	{
		OutSum = 0.f;

		int32 Num = InValues.Num();
		const float* InData = InValues.GetData();

		for (int32 i = 0; i < Num; i++)
		{
			OutSum += InData[i];
		}
	}

	void ArraySum(const FAlignedFloatBuffer& InValues, float& OutSum)
	{
		OutSum = 0.f;

		const int32 Num = InValues.Num();
		const int32 NumToSimd = Num & MathIntrinsics::SimdMask;
		const int32 NumNotToSimd = Num & MathIntrinsics::NotSimdMask;

		const float* InData = InValues.GetData();

		if (NumToSimd)
		{
			VectorRegister4Float Total = VectorSetFloat1(0.f);

			for (int32 i = 0; i < NumToSimd; i += 4)
			{
				VectorRegister4Float VectorData = VectorLoadAligned(&InData[i]);
				Total = VectorAdd(Total, VectorData);
			}

			MS_ALIGN(16) float Val[4] GCC_ALIGN(16);
			VectorStoreAligned(Total, Val);
			OutSum = Val[0] + Val[1] + Val[2] + Val[3];
		}

		if (NumNotToSimd)
		{
			TArrayView<const float> ValuesView(&InData[NumToSimd],  NumNotToSimd);

			float ExtraSum = 0.f;

			ArraySum(ValuesView, ExtraSum);

			OutSum += ExtraSum;
		}
	}

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

	void ArrayMeanSquared(TArrayView<const float> InView, float& OutMean)
	{
		OutMean = 0.0f;

		const int32 Num = InView.Num();

		if (Num < 1)
		{
			return;
		}

		const float* DataPtr = InView.GetData();

		for (int32 i = 0; i < Num; i++)
		{
			OutMean += DataPtr[i] * DataPtr[i];
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

	void ArrayAbs(TArrayView<const float> InBuffer, TArrayView<float> OutBuffer)
	{
		const int32 Num = InBuffer.Num();
		check(OutBuffer.Num() == Num);

		const float* InData = InBuffer.GetData();
		float* OutData = OutBuffer.GetData();

		for (int32 i = 0; i < Num; i++)
		{
			OutData[i] = FMath::Abs(InData[i]);
		}
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

	void ArrayMultiplyInPlace(TArrayView<const float> InValues1, TArrayView<float> InValues2)
	{
		check(InValues1.Num() == InValues2.Num());

		const int32 Num = InValues1.Num();

		const float* InData1 = InValues1.GetData();
		float* InData2 = InValues2.GetData();

		for (int32 i = 0; i < Num; i++)
		{
			InData2[i] *= InData1[i];
		}
	}

	void ArrayMultiplyInPlace(const FAlignedFloatBuffer& InValues1, FAlignedFloatBuffer& InValues2)
	{
		check(InValues1.Num() == InValues2.Num());

		const int32 Num = InValues1.Num();
		const int32 NumToSimd = Num & MathIntrinsics::SimdMask;
		const int32 NumNotToSimd = Num & MathIntrinsics::NotSimdMask;

		const float* InData1 = InValues1.GetData();
		float* InData2 = InValues2.GetData();

		if (NumToSimd)
		{
			MultiplyBuffersInPlace(InData1, InData2, NumToSimd);
		}

		if (NumNotToSimd)
		{
			TArrayView<const float> ValuesView1(&InData1[NumToSimd],  NumNotToSimd);
			TArrayView<float> ValuesView2(&InData2[NumToSimd],  NumNotToSimd);

			ArrayMultiplyInPlace(ValuesView1, ValuesView2);
		}
	}

	void ArrayComplexMultiplyInPlace(TArrayView<const float> InValues1, TArrayView<float> InValues2)
	{
		check(InValues1.Num() == InValues2.Num());

		const int32 Num = InValues1.Num();

		// Needs to be in interleaved format.
		check((Num % 2) == 0);

		const float* InData1 = InValues1.GetData();
		float* InData2 = InValues2.GetData();

		for (int32 i = 0; i < Num; i += 2)
		{
			float Real = (InData1[i] * InData2[i]) - (InData1[i + 1] * InData2[i + 1]);
			float Imag = (InData1[i] * InData2[i + 1]) + (InData1[i + 1] * InData2[i]);
			InData2[i] = Real;
			InData2[i + 1] = Imag;
		}
	}

	void ArrayComplexMultiplyInPlace(const FAlignedFloatBuffer& InValues1, FAlignedFloatBuffer& InValues2)
	{
		check(InValues1.Num() == InValues2.Num());

		const int32 Num = InValues1.Num();
		const int32 NumToSimd = Num & MathIntrinsics::SimdMask;
		const int32 NumNotToSimd = Num & MathIntrinsics::NotSimdMask;

		const float* InData1 = InValues1.GetData();
		float* InData2 = InValues2.GetData();

		if (NumToSimd)
		{
			const VectorRegister4Float RealSignFlip = MakeVectorRegister(-1.f, 1.f, -1.f, 1.f);

			for (int32 i = 0; i < NumToSimd; i += 4)
			{
				VectorRegister4Float VectorData1 = VectorLoadAligned(&InData1[i]);
				VectorRegister4Float VectorData2 = VectorLoadAligned(&InData2[i]);

				VectorRegister4Float VectorData1Real = VectorSwizzle(VectorData1, 0, 0, 2, 2);
				VectorRegister4Float VectorData1Imag = VectorSwizzle(VectorData1, 1, 1, 3, 3);
				VectorRegister4Float VectorData2Swizzle = VectorSwizzle(VectorData2, 1, 0, 3, 2);

				VectorRegister4Float Result = VectorMultiply(VectorData1Imag, VectorData2Swizzle);
				Result = VectorMultiply(Result, RealSignFlip);
				Result = VectorMultiplyAdd(VectorData1Real, VectorData2, Result);

				VectorStoreAligned(Result, &InData2[i]);
			}
		}

		if (NumNotToSimd)
		{
			TArrayView<const float> ValuesView1(&InData1[NumToSimd],  NumNotToSimd);
			TArrayView<float> ValuesView2(&InData2[NumToSimd],  NumNotToSimd);

			ArrayComplexMultiplyInPlace(ValuesView1, ValuesView2);
		}
	}

	void ArrayMultiplyByConstantInPlace(TArrayView<float> InValues, float InMultiplier)
	{
		const int32 Num = InValues.Num();
		float* InData = InValues.GetData();

		for (int32 i = 0; i < Num; i++)
		{
			InData[i] *= InMultiplier;
		}
	}

	void ArrayMultiplyByConstantInPlace(FAlignedFloatBuffer& InValues, float InMultiplier)
	{
		const int32 Num = InValues.Num();
		const int32 NumToSimd = Num & MathIntrinsics::SimdMask;
		const int32 NumNotToSimd = Num & MathIntrinsics::NotSimdMask;

		float* InData = InValues.GetData();

		if (NumToSimd)
		{
			MultiplyBufferByConstantInPlace(InData, NumToSimd, InMultiplier);
		}

		if (NumNotToSimd)
		{
			TArrayView<float> ValuesView(&InData[NumToSimd],  NumNotToSimd);

			ArrayMultiplyByConstantInPlace(ValuesView, InMultiplier);
		}
	}

	void ArrayAddInPlace(TArrayView<const float> InValues, TArrayView<float> InAccumulateValues)
	{
		check(InValues.Num() == InAccumulateValues.Num());

		const int32 Num = InValues.Num();

		const float* InData = InValues.GetData();
		float* InAccumulateData = InAccumulateValues.GetData();

		for (int32 i = 0; i < Num; i++)
		{
			InAccumulateData[i] += InData[i];
		}
	}

	void ArrayAddInPlace(const FAlignedFloatBuffer& InValues, FAlignedFloatBuffer& InAccumulateValues)
	{
		check(InValues.Num() == InAccumulateValues.Num());

		const int32 Num = InAccumulateValues.Num();
		const int32 NumToSimd = Num & MathIntrinsics::SimdMask;
		const int32 NumNotToSimd = Num & MathIntrinsics::NotSimdMask;

		const float* InData = InValues.GetData();
		float* InAccumulateData = InAccumulateValues.GetData();

		for (int32 i = 0; i < NumToSimd; i += 4)
		{
			VectorRegister4Float VectorData = VectorLoadAligned(&InData[i]);
			VectorRegister4Float VectorAccumData = VectorLoadAligned(&InAccumulateData[i]);

			VectorRegister4Float VectorOut = VectorAdd(VectorData, VectorAccumData);
			VectorStoreAligned(VectorOut, &InAccumulateData[i]);
		}

		if (NumNotToSimd)
		{
			TArrayView<const float> ValuesView(&InData[NumToSimd],  NumNotToSimd);
			TArrayView<float> AccumulateView(&InAccumulateData[NumToSimd], NumNotToSimd);

			ArrayAddInPlace(ValuesView, AccumulateView);
		}
	}

	void ArrayMultiplyAddInPlace(TArrayView<const float> InValues, float InMultiplier, TArrayView<float> InAccumulateValues)
	{
		check(InValues.Num() == InAccumulateValues.Num());
		
		const int32 Num = InValues.Num();

		const float* InData = InValues.GetData();
		float* InAccumulateData = InAccumulateValues.GetData();

		for (int32 i = 0; i < Num; i++)
		{
			InAccumulateData[i] += InData[i] * InMultiplier;
		}
	}

	void ArrayMultiplyAddInPlace(const FAlignedFloatBuffer& InValues, float InMultiplier, FAlignedFloatBuffer& InAccumulateValues)
	{
		check(InValues.Num() == InAccumulateValues.Num());

		const int32 Num = InAccumulateValues.Num();
		const int32 NumToSimd = Num & MathIntrinsics::SimdMask;
		const int32 NumNotToSimd = Num & MathIntrinsics::NotSimdMask;

		const float* InData = InValues.GetData();
		float* InAccumulateData = InAccumulateValues.GetData();

		MixInBufferFast(InData, InAccumulateData, NumToSimd, InMultiplier);

		if (NumNotToSimd)
		{
			TArrayView<const float> ValuesView(&InData[NumToSimd],  NumNotToSimd);
			TArrayView<float> AccumulateView(&InAccumulateData[NumToSimd], NumNotToSimd);

			ArrayMultiplyAddInPlace(ValuesView, InMultiplier, AccumulateView);
		}
	}

	void ArrayLerpAddInPlace(TArrayView<const float> InValues, float InStartMultiplier, float InEndMultiplier, TArrayView<float> InAccumulateValues)
	{
		check(InValues.Num() == InAccumulateValues.Num());

		const int32 Num = InValues.Num();

		const float* InData = InValues.GetData();
		float* InAccumulateData = InAccumulateValues.GetData();

		const float Delta = (InEndMultiplier - InStartMultiplier) / FMath::Max(1.f, static_cast<float>(Num - 1));
		float Multiplier = InStartMultiplier;

		for (int32 i = 0; i < Num; i++)
		{
			InAccumulateData[i] += InData[i] * Multiplier;
			Multiplier += Delta;
		}
	}

	void ArrayLerpAddInPlace(const FAlignedFloatBuffer& InValues, float InStartMultiplier, float InEndMultiplier, FAlignedFloatBuffer& InAccumulateValues)
	{
		check(InValues.Num() == InAccumulateValues.Num());

		const int32 Num = InAccumulateValues.Num();
		const int32 NumToSimd = Num & MathIntrinsics::SimdMask;
		const int32 NumNotToSimd = Num & MathIntrinsics::NotSimdMask;

		const float* InData = InValues.GetData();
		float* InAccumulateData = InAccumulateValues.GetData();
		
		const float Delta = (InEndMultiplier - InStartMultiplier) / FMath::Max(1.f, static_cast<float>(Num - 1));

		const float FourByDelta = 4.f * Delta;
		VectorRegister4Float VectorDelta = MakeVectorRegister(FourByDelta, FourByDelta, FourByDelta, FourByDelta);
		VectorRegister4Float VectorMultiplier = MakeVectorRegister(InStartMultiplier, InStartMultiplier + Delta, InStartMultiplier + 2.f * Delta, InStartMultiplier + 3.f * Delta);

		for (int32 i = 0; i < NumToSimd; i += 4)
		{
			VectorRegister4Float VectorData = VectorLoadAligned(&InData[i]);
			VectorRegister4Float VectorAccumData = VectorLoadAligned(&InAccumulateData[i]);

			VectorRegister4Float VectorOut = VectorMultiplyAdd(VectorData, VectorMultiplier, VectorAccumData);
			VectorMultiplier = VectorAdd(VectorMultiplier, VectorDelta);

			VectorStoreAligned(VectorOut, &InAccumulateData[i]);
		}

		if (NumNotToSimd)
		{
			TArrayView<const float> ValuesView(&InData[NumToSimd],  NumNotToSimd);
			TArrayView<float> AccumulateView(&InAccumulateData[NumToSimd], NumNotToSimd);

			ArrayLerpAddInPlace(ValuesView, InStartMultiplier + NumToSimd * Delta, InEndMultiplier, AccumulateView);
		}
	}

	void ArraySubtractByConstantInPlace(TArrayView<float> InValues, float InSubtrahend)
	{
		const int32 Num = InValues.Num();
		float* InValuesData = InValues.GetData();
		for (int32 i = 0; i < Num; i++)
		{
			InValuesData[i] -= InSubtrahend;
		}
	}

	void ArraySubtractByConstantInPlace(FAlignedFloatBuffer& InValues, float InSubtrahend)
	{
		const int32 Num = InValues.Num();
		const int32 NumToSimd = Num & MathIntrinsics::SimdMask;
		const int32 NumNotToSimd = Num & MathIntrinsics::NotSimdMask;

		float* InData = InValues.GetData();

		const VectorRegister4Float VectorSubtrahend = VectorSetFloat1(InSubtrahend);

		for (int32 i = 0; i < NumToSimd; i += 4)
		{
			VectorRegister4Float VectorData = VectorLoadAligned(&InData[i]);
			VectorData = VectorSubtract(VectorData, VectorSubtrahend);
			VectorStoreAligned(VectorData, &InData[i]);
		}

		if (NumNotToSimd)
		{
			TArrayView<float> View(&InData[NumToSimd], NumNotToSimd);
			ArraySubtractByConstantInPlace(View, InSubtrahend);
		}
	}

	void ArraySubtract(TArrayView<const float> InMinuend, TArrayView<const float> InSubtrahend, TArray<float>& OutArray)
	{
		const int32 Num = InMinuend.Num();

		checkf(Num == InSubtrahend.Num() && Num == OutArray.Num(), TEXT("InMinuend, InSubtrahend, and OutArray must have equal Num elements (%d vs %d vs %d)"), Num, InSubtrahend.Num(), OutArray.Num());
		

		if (Num < 1)
		{
			return;
		}
		
		const float* MinuendPtr = InMinuend.GetData();
		const float* SubtrahendPtr = InSubtrahend.GetData();
		float* OutPtr = OutArray.GetData();

		for (int32 i = 0; i < Num; i++)
		{
			OutPtr[i] = MinuendPtr[i] - SubtrahendPtr[i];
		}
	}

	void ArraySubtract(const FAlignedFloatBuffer& InMinuend, const FAlignedFloatBuffer& InSubtrahend, FAlignedFloatBuffer& OutArray)
	{
		BufferSubtractFast(InMinuend, InSubtrahend, OutArray);
	}

	void ArraySquare(TArrayView<const float> InValues, TArrayView<float> OutValues)
	{
		check(InValues.Num() == OutValues.Num());

		const uint32 Num = InValues.Num();
		const uint32 NumToSimd = Num & MathIntrinsics::SimdMask;

		const float* InData = InValues.GetData();
		float* OutData = OutValues.GetData();

		for (uint32 i = 0; i < NumToSimd; i += 4)
		{
			VectorRegister4Float VectorData = VectorLoad(&InData[i]);
			VectorData = VectorMultiply(VectorData, VectorData);
			VectorStore(VectorData, &OutData[i]);
		}

		for (uint32 i = NumToSimd; i < Num; i++)
		{
			OutData[i] = InData[i] * InData[i];
		}
	}

	void ArraySquareInPlace(TArrayView<float> InValues)
	{
		const uint32 Num = InValues.Num();
		const uint32 NumToSimd = Num & MathIntrinsics::SimdMask;

		float* InData = InValues.GetData();

		for (uint32 i = 0; i < NumToSimd; i += 4)
		{
			VectorRegister4Float VectorData = VectorLoad(&InData[i]);
			VectorData = VectorMultiply(VectorData, VectorData);
			VectorStore(VectorData, &InData[i]);
		}

		for (uint32 i = NumToSimd; i < Num; i++)
		{
			InData[i] = InData[i] * InData[i];
		}
	}

	void ArraySqrtInPlace(TArrayView<float> InValues)
	{
		const int32 Num = InValues.Num();
		float* InValuesData = InValues.GetData();

		for (int32 i = 0; i < Num; i++)
		{
			InValues[i] = FMath::Sqrt(InValues[i]);
		}
	}

	void ArrayComplexConjugate(TArrayView<const float> InValues, TArrayView<float> OutValues)
	{
		check(OutValues.Num() == InValues.Num());
		check((InValues.Num() % 2) == 0);

		int32 Num = InValues.Num();

		const float* InData = InValues.GetData();
		float* OutData = OutValues.GetData();
		
		for (int32 i = 0; i < Num; i+= 2)
		{
			OutData[i] = InData[i];
			OutData[i + 1] = -InData[i + 1];
		}
	}

	void ArrayComplexConjugate(const FAlignedFloatBuffer& InValues, FAlignedFloatBuffer& OutValues)
	{
		check(OutValues.Num() == InValues.Num());

		const int32 Num = InValues.Num();
		const int32 NumToSimd = Num & MathIntrinsics::SimdMask;
		const int32 NumNotToSimd = Num & MathIntrinsics::NotSimdMask;

		const float* InData = InValues.GetData();
		float* OutData = OutValues.GetData();

		const VectorRegister4Float ConjugateMult = MakeVectorRegister(1.f, -1.f, 1.f, -1.f);

		for (int32 i = 0; i < NumToSimd; i += 4)
		{
			VectorRegister4Float VectorData = VectorLoadAligned(&InData[i]);
			
			VectorData = VectorMultiply(VectorData, ConjugateMult);

			VectorStoreAligned(VectorData, &OutData[i]);
		}

		if (NumNotToSimd)
		{
			TArrayView<const float> InView(&InData[NumToSimd], NumNotToSimd);
			TArrayView<float> OutView(&OutData[NumToSimd], NumNotToSimd);

			ArrayComplexConjugate(InView, OutView);
		}
	}

	void ArrayComplexConjugateInPlace(TArrayView<float> InValues)
	{
		check((InValues.Num() % 2) == 0);

		int32 Num = InValues.Num();

		float* InData = InValues.GetData();
		
		for (int32 i = 1; i < Num; i+= 2)
		{
			InData[i] *= -1.f;
		}
	}

	void ArrayComplexConjugateInPlace(FAlignedFloatBuffer& InValues)
	{
		const int32 Num = InValues.Num();
		const int32 NumToSimd = Num & MathIntrinsics::SimdMask;
		const int32 NumNotToSimd = Num & MathIntrinsics::NotSimdMask;

		float* InData = InValues.GetData();

		const VectorRegister4Float ConjugateMult = MakeVectorRegister(1.f, -1.f, 1.f, -1.f);

		for (int32 i = 0; i < NumToSimd; i += 4)
		{
			VectorRegister4Float VectorData = VectorLoadAligned(&InData[i]);
			
			VectorData = VectorMultiply(VectorData, ConjugateMult);

			VectorStoreAligned(VectorData, &InData[i]);
		}

		if (NumNotToSimd)
		{
			TArrayView<float> InView(&InData[NumToSimd], NumNotToSimd);

			ArrayComplexConjugateInPlace(InView);
		}
	}

	void ArrayMagnitudeToDecibelInPlace(TArrayView<float> InValues, float InMinimumDb)
	{
		const int32 Num = InValues.Num();
		float* InValuesData = InValues.GetData();

		const float Minimum = FMath::Exp(InMinimumDb * MathIntrinsics::Loge10 / 20.f);

		for (int32 i = 0; i < Num; i++)
		{
			InValuesData[i] = FMath::Max(InValuesData[i], Minimum);
			InValuesData[i] = 20.f * FMath::Loge(InValuesData[i]) / MathIntrinsics::Loge10;
		}
	}

	void ArrayMagnitudeToDecibelInPlace(FAlignedFloatBuffer& InValues, float InMinimumDb)
	{
		const int32 Num = InValues.Num();
		const int32 NumToSimd = Num & MathIntrinsics::SimdMask;
		const int32 NumNotToSimd = Num & MathIntrinsics::NotSimdMask;

		float* InData = InValues.GetData();

		const float Scale = 20.f / MathIntrinsics::Loge10;
		const float Minimum = FMath::Exp(InMinimumDb * MathIntrinsics::Loge10 / 20.f);

		const VectorRegister4Float VectorScale = VectorSetFloat1(Scale);
		const VectorRegister4Float VectorMinimum = VectorSetFloat1(Minimum);

		for (int32 i = 0; i < NumToSimd; i += 4)
		{
			VectorRegister4Float VectorData = VectorLoadAligned(&InData[i]);
			
			VectorData = VectorMax(VectorData, VectorMinimum);
			VectorData = VectorLog(VectorData);
			VectorData = VectorMultiply(VectorData, VectorScale);

			VectorStoreAligned(VectorData, &InData[i]);
		}

		if (NumNotToSimd)
		{
			TArrayView<float> InView(&InData[NumToSimd], NumNotToSimd);
			ArrayMagnitudeToDecibelInPlace(InView, InMinimumDb);
		}
	}

	void ArrayPowerToDecibelInPlace(TArrayView<float> InValues, float InMinimumDb)
	{
		const int32 Num = InValues.Num();
		float* InValuesData = InValues.GetData();

		const float Minimum = FMath::Exp(InMinimumDb * MathIntrinsics::Loge10 / 10.f);

		for (int32 i = 0; i < Num; i++)
		{
			InValuesData[i] = FMath::Max(InValuesData[i], Minimum);
			InValuesData[i] = 10.f * FMath::Loge(InValuesData[i]) / MathIntrinsics::Loge10;
		}
	}

	void ArrayPowerToDecibelInPlace(FAlignedFloatBuffer& InValues, float InMinimumDb)
	{
		const int32 Num = InValues.Num();
		const int32 NumToSimd = Num & MathIntrinsics::SimdMask;
		const int32 NumNotToSimd = Num & MathIntrinsics::NotSimdMask;

		float* InData = InValues.GetData();

		const float Scale = 10.f / MathIntrinsics::Loge10;
		const float Minimum = FMath::Exp(InMinimumDb * MathIntrinsics::Loge10 / 10.f);

		const VectorRegister4Float VectorMinimum = VectorSetFloat1(Minimum);
		const VectorRegister4Float VectorScale = VectorSetFloat1(Scale);

		for (int32 i = 0; i < NumToSimd; i += 4)
		{
			VectorRegister4Float VectorData = VectorLoadAligned(&InData[i]);

			VectorData = VectorMax(VectorData, VectorMinimum);
			VectorData = VectorLog(VectorData);
			VectorData = VectorMultiply(VectorData, VectorScale);

			VectorStoreAligned(VectorData, &InData[i]);
		}

		if (NumNotToSimd)
		{
			TArrayView<float> InView(&InData[NumToSimd], NumNotToSimd);
			ArrayPowerToDecibelInPlace(InView, InMinimumDb);
		}
	}

	void ArrayComplexToPower(TArrayView<const float> InComplexValues, TArrayView<float> OutPowerValues)
	{
		check((InComplexValues.Num() % 2) == 0);
		check(InComplexValues.Num() == (OutPowerValues.Num() * 2));

		const int32 NumOut = OutPowerValues.Num();

		const float* InComplexData = InComplexValues.GetData();
		float* OutPowerData = OutPowerValues.GetData();

		for (int32 i = 0; i < NumOut; i++)
		{
			int32 ComplexPos = 2 * i;

			float RealValue = InComplexData[ComplexPos];
			float ImagValue = InComplexData[ComplexPos + 1];

			OutPowerData[i] = (RealValue * RealValue) + (ImagValue * ImagValue);
		}
	}

	void ArrayComplexToPower(const FAlignedFloatBuffer& InComplexValues, FAlignedFloatBuffer& OutPowerValues)
	{
		check((InComplexValues.Num() % 2) == 0);
		check(InComplexValues.Num() == (OutPowerValues.Num() * 2));

		const int32 NumOut = OutPowerValues.Num();
		const int32 NumToSimd = NumOut & MathIntrinsics::SimdMask;
		const int32 NumNotToSimd = NumOut & MathIntrinsics::NotSimdMask;

		const float* InComplexData = InComplexValues.GetData();
		float* OutPowerData = OutPowerValues.GetData();

		for (int32 i = 0; i < NumToSimd; i += 4)
		{
			VectorRegister4Float VectorComplex1 = VectorLoadAligned(&InComplexData[2 * i]);
			VectorRegister4Float VectorSquared1 = VectorMultiply (VectorComplex1, VectorComplex1);

			VectorRegister4Float VectorComplex2 = VectorLoadAligned(&InComplexData[(2 * i) + 4]);
			VectorRegister4Float VectorSquared2 = VectorMultiply (VectorComplex2, VectorComplex2);

			VectorRegister4Float VectorSquareReal = VectorShuffle(VectorSquared1, VectorSquared2, 0, 2, 0, 2);
			VectorRegister4Float VectorSquareImag = VectorShuffle(VectorSquared1, VectorSquared2, 1, 3, 1, 3);

			VectorRegister4Float VectorOut = VectorAdd(VectorSquareReal, VectorSquareImag);
			
			VectorStoreAligned(VectorOut, &OutPowerData[i]);
		}

		if (NumNotToSimd)
		{
			TArrayView<const float> ComplexView(&InComplexData[2 * NumToSimd], 2 * NumNotToSimd);
			TArrayView<float> PowerView(&OutPowerData[NumToSimd], NumNotToSimd);

			ArrayComplexToPower(ComplexView, PowerView);
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

		// Fill up the kernel with empty rows
		Kernel.Init(EmptyRow, NumOut);
	}

	FContiguousSparse2DKernelTransform::~FContiguousSparse2DKernelTransform()
	{
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

	void FContiguousSparse2DKernelTransform::TransformArray(TArrayView<const float> InView, FAlignedFloatBuffer& OutArray) const
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
