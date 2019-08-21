// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace Audio
{
	/** Cumulative sum of array.
	 *
	 *  InData contains data to be cumulatively summed.
	 *  OutData contains sum and is same size as InData.
	 */
	void ArrayCumulativeSum(const TArray<float>& InData, TArray<float>& OutData);

	/** Mean filter of array.
	 *
	 *  Note: Uses standard biased mean estimator of Sum(x) / Count(x).
	 *  Note: At array boundaries, this algorithm truncates windows where no valid array data exists. Values calculated with truncated windows have corresponding increased variances. 
	 *
	 *  InData contains data to be filtered.
	 *  WindowSize determines the number of samples from InData analyzed to produce a value in OutData.
	 *  WindowOrigin describes the offset from the windows first sample to the index of OutData. For example, if WindowOrigin = WindowSize/4, then OutData[i] = Mean(InData[i - Window/4 : i + 3 * Window / 4]).
	 *  OutData contains the produceds data.
	 */
	void ArrayMeanFilter(const TArray<float>& InData, int32 WindowSize, int32 WindowOrigin, TArray<float>& OutData);

	/** Max filter of array.
	 *
	 *  Note: At array boundaries, this algorithm truncates windows where no valid array data exists. 
	 *
	 *  InData contains data to be filtered.
	 *  WindowSize determines the number of samples from InData analyzed to produce a value in OutData.
	 *  WindowOrigin describes the offset from the windows first sample to the index of OutData. For example, if WindowOrigin = WindowSize/4, then OutData[i] = Max(InData[i - Window/4 : i + 3 * Window / 4]).
	 *  OutData contains the produceds data.
	 */
	void ArrayMaxFilter(const TArray<float>& InData, int32 WindowSize, int32 WindowOrigin, TArray<float>& OutData);

	/** Computes the EuclideanNorm of the InArray. Same as calculating the energy in window. */
	void ArrayGetEuclideanNorm(const TArray<float>& InArray, float& OutEuclideanNorm);

	/** Multiplies each element in InArray by InMultiplier */
	void ArrayMultiplyByConstantInPlace(TArray<float>& InArray, const float InMultiplier);
}
