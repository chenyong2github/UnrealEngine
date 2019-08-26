// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DSP/FloatArrayMath.h"
#include "CoreMinimal.h"

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
			//OutDataPtr++;
			*OutDataPtr = Temp;
			//InDataPtr++;
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
		
		
		const int32 LastIndexBeforeEndBoundaryCondition = FMath::Max(WindowOrigin, Num - WindowSize + WindowOrigin + 1);
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

	void ArrayMultiplyByConstantInPlace(TArray<float>& InArray, const float InMultiplier)
	{
		const int32 Num = InArray.Num();
		float* InArrayData = InArray.GetData();
		for (int32 i = 0; i < Num; i++)
		{
			InArrayData[i] *= InMultiplier;
		}
	}
}
