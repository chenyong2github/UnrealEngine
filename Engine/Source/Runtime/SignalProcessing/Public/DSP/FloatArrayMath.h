// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DSP/BufferVectorOperations.h"

namespace Audio
{
	/** Cumulative sum of array.
	 *
	 *  InView contains data to be cumulatively summed.
	 *  OutData contains sum and is same size as InView.
	 */
	SIGNALPROCESSING_API void ArrayCumulativeSum(TArrayView<const float> InView, TArray<float>& OutData);

	/** Mean filter of array.
	 *
	 *  Note: Uses standard biased mean estimator of Sum(x) / Count(x).
	 *  Note: At array boundaries, this algorithm truncates windows where no valid array data exists. Values calculated with truncated windows have corresponding increased variances. 
	 *
	 *  InView contains data to be filtered.
	 *  WindowSize determines the number of samples from InView analyzed to produce a value in OutData.
	 *  WindowOrigin describes the offset from the windows first sample to the index of OutData. For example, if WindowOrigin = WindowSize/4, then OutData[i] = Mean(InView[i - Window/4 : i + 3 * Window / 4]).
	 *  OutData contains the produceds data.
	 */
	SIGNALPROCESSING_API void ArrayMeanFilter(TArrayView<const float> InView, int32 WindowSize, int32 WindowOrigin, TArray<float>& OutData);

	/** Max filter of array.
	 *
	 *  Note: At array boundaries, this algorithm truncates windows where no valid array data exists. 
	 *
	 *  InView contains data to be filtered.
	 *  WindowSize determines the number of samples from InView analyzed to produce a value in OutData.
	 *  WindowOrigin describes the offset from the windows first sample to the index of OutData. For example, if WindowOrigin = WindowSize/4, then OutData[i] = Max(InView[i - Window/4 : i + 3 * Window / 4]).
	 *  OutData contains the produceds data.
	 */
	SIGNALPROCESSING_API void ArrayMaxFilter(TArrayView<const float> InView, int32 WindowSize, int32 WindowOrigin, TArray<float>& OutData);

	/** Computes the EuclideanNorm of the InView. Same as calculating the energy in window. */
	SIGNALPROCESSING_API void ArrayGetEuclideanNorm(TArrayView<const float> InView, float& OutEuclideanNorm);

	/** Clamp values in an array.
	 *
	 *  InView is a view of a float array to be clamped.
	 *  InMin is the minimum value allowable in the array.
	 *  InMax is the maximum value allowable in the array.
	 */
	SIGNALPROCESSING_API void ArrayClampInPlace(TArrayView<float> InView, float InMin, float InMax);

	/** Multiplies each element in InView by InMultiplier */
	SIGNALPROCESSING_API void ArrayMultiplyByConstantInPlace(TArrayView<float> InView, float InMultiplier);

	/** Subtract value from each element in InView */
	SIGNALPROCESSING_API void ArraySubtractByConstantInPlace(TArrayView<float> InView, float InSubtrahend);

	/** Convert magnitude values to decibel values in place. db = 20 * log10(val) */
	SIGNALPROCESSING_API void ArrayMagnitudeToDecibelInPlace(TArrayView<float> InView);

	/** Convert power values to decibel values in place. db = 10 * log10(val) */
	SIGNALPROCESSING_API void ArrayPowerToDecibelInPlace(TArrayView<float> InView);

	/** FContiguousSparse2DKernelTransform
	 *
	 *  FContiguousSparse2DKernelTransform applies a matrix transformation to an input array. 
	 *  [OutArray] = [[Kernal]][InView]  
	 *
	 *  It provides some optimization by exploit the contiguous and sparse qualities of the kernel rows,
	 *  which allows it to skip multiplications with the number zero. 
	 *
	 *  It works with non-sparse and non-contiguous kernels as well, but will be more computationally 
	 *  expensive than a naive implementation. Also, only takes advantage of sparse contiguous rows, not columns.
	 */
	class SIGNALPROCESSING_API FContiguousSparse2DKernelTransform
	{
		struct FRow
		{
			int32 StartIndex;
			TArray<float> OffsetValues;
		};

	public:

		/**
		 * NumInElements sets the expected number of input array elements as well as the number of elements in a row.
		 * NumOutElements sets the number of output array elements as well as the number or rows.
		 */
		FContiguousSparse2DKernelTransform(const int32 NumInElements, const int32 NumOutElements);

		/** Returns the required size of the input array */
		int32 GetNumInElements() const;

		/** Returns the size of the output array */
		int32 GetNumOutElements() const;
	
		/** Set the kernel values for an individual row.
		 *
		 *  RowIndex determines which row is being set.
		 *  StartIndex denotes the offset into the row where the OffsetValues will be inserted.
		 *  OffsetValues contains the contiguous chunk of values which represent all the nonzero elements in the row.
		 */
		void SetRow(const int32 RowIndex, const int32 StartIndex, TArrayView<const float> OffsetValues);

		/** Transforms the input array given the kernel.
		 *
		 *  InView is the array to be transformed. It must have `NumInElements` number of elements.
		 *  OutArray is the transformed array. It will have `NumOutElements` number of elements.
		 */
		void TransformArray(TArrayView<const float> InView, TArray<float>& OutArray) const;

		/** Transforms the input array given the kernel.
		 *
		 *  InView is the array to be transformed. It must have `NumInElements` number of elements.
		 *  OutArray is the transformed array. It will have `NumOutElements` number of elements.
		 */
		void TransformArray(TArrayView<const float> InView, AlignedFloatBuffer& OutArray) const;

		/** Transforms the input array given the kernel.
		 *
		 *  InArray is the array to be transformed. It must have `NumInElements` number of elements.
		 *  OutArray is the transformed array. It must be allocated to hold at least NumOutElements. 
		 */
		void TransformArray(const float* InArray, float* OutArray) const;

	private:

		int32 NumIn;
		int32 NumOut;
		TArray<FRow> Kernel;
	};

}
