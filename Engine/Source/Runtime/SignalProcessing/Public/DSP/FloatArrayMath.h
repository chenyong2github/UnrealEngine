// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DSP/BufferVectorOperations.h"

namespace Audio
{
	SIGNALPROCESSING_API void ArraySum(TArrayView<const float> InValues, float& OutSum);
	SIGNALPROCESSING_API void ArraySum(const AlignedFloatBuffer& InValues, float& OutSum);

	/** Cumulative sum of array.
	 *
	 *  InView contains data to be cumulatively summed.
	 *  OutData contains sum and is same size as InView.
	 */
	SIGNALPROCESSING_API void ArrayCumulativeSum(TArrayView<const float> InView, TArray<float>& OutData);

	/** Mean of array. Equivalent to Sum(InView) / InView.Num()
	 *
	 *  InView contains data to be analyzed.
	 *  OutMean contains the result.
	 */
	SIGNALPROCESSING_API void ArrayMean(TArrayView<const float> InView, float& OutMean);

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

	/** Absolute value of array elements in place.
	 *
	 *  InView contains the data to be manipulated.
	 */
	SIGNALPROCESSING_API void ArrayAbsInPlace(TArrayView<float> InView);

	/** Clamp minimum value of array in place.
	 *
	 *  InView contains data to be clamped.
	 *  InMin contains the minimum value allowable in InView.
	 */
	SIGNALPROCESSING_API void ArrayClampMinInPlace(TArrayView<float> InView, float InMin);

	/** Clamp maximum value of array in place.
	 *
	 *  InView contains data to be clamped.
	 *  InMax contains the maximum value allowable in InView.
	 */
	SIGNALPROCESSING_API void ArrayClampMaxInPlace(TArrayView<float> InView, float InMax);

	/** Clamp values in an array.
	 *
	 *  InView is a view of a float array to be clamped.
	 *  InMin is the minimum value allowable in the array.
	 *  InMax is the maximum value allowable in the array.
	 */
	SIGNALPROCESSING_API void ArrayClampInPlace(TArrayView<float> InView, float InMin, float InMax);

	/** Scale an array so the minimum is 0 and the maximum is 1
	 *
	 *  InView is the view of a float array with the input data.
	 *  OutArray is an array which will hold the normalized data.
	 */ 
	SIGNALPROCESSING_API void ArrayMinMaxNormalize(TArrayView<const float> InView, TArray<float>& OutArray);

	/** Multiplies two arrays element-wise. Stores result in InValues2*/
	SIGNALPROCESSING_API void ArrayMultiplyInPlace(TArrayView<const float> InValues1, TArrayView<float> InValues2);
	SIGNALPROCESSING_API void ArrayMultiplyInPlace(const AlignedFloatBuffer& InValues1, AlignedFloatBuffer& InValues2);

	/** Multiplies two complex valued arrays element-wise. 
	 * This assumes elements are in interleaved format [real_0, imag_0, ..., real_N, imag_N]
	 * Stores result in InValues2
	 */
	SIGNALPROCESSING_API void ArrayComplexMultiplyInPlace(TArrayView<const float> InValues1, TArrayView<float> InValues2);
	SIGNALPROCESSING_API void ArrayComplexMultiplyInPlace(const AlignedFloatBuffer& InValues1, AlignedFloatBuffer& InValues2);

	/** Multiplies each element in InView by InMultiplier */
	SIGNALPROCESSING_API void ArrayMultiplyByConstantInPlace(TArrayView<float> InValues, float InMultiplier);
	SIGNALPROCESSING_API void ArrayMultiplyByConstantInPlace(AlignedFloatBuffer& InValues, float InMultiplier);

	/** Add arrays element-wise in place. InAccumulateValues[i] += InValues[i]
	 *
	 *  InValues is the array to add.
	 *  InAccumulateValues is the array which holds the sum.
	 */
	SIGNALPROCESSING_API void ArrayAddInPlace(TArrayView<const float> InValues, TArrayView<float> InAccumulateValues);
	SIGNALPROCESSING_API void ArrayAddInPlace(const AlignedFloatBuffer& InValues, AlignedFloatBuffer& InAccumulateValues);

	/** Multiply Add arrays element-wise in place. InAccumulateValues[i] += InMultiplier * InValues[i]
	 *
	 *  @param InValues - The array to add.
	 *  @param InMultiplier - The value to multiply against InValues
	 *  @param InAccumulateValues - The array which holds the sum.
	 */
	SIGNALPROCESSING_API void ArrayMultiplyAddInPlace(TArrayView<const float> InValues, float InMultiplier, TArrayView<float> InAccumulateValues);
	SIGNALPROCESSING_API void ArrayMultiplyAddInPlace(const AlignedFloatBuffer& InValues, float InMultiplier, AlignedFloatBuffer& InAccumulateValues);

	/** Linearly Interpolate Add arrays element-wise in place. InAccumulateValues[i] += ((1 - alpha) * InStartMultiplier + alpha * InEndMultipler) * InValues[i]
	 * Interpolation is performed over the length of the array.
	 *
	 *  @param InValues - The array to add.
	 *  @param InStartMultiplier - The beginning value to multiply against InValues
	 *  @param InEndMultiplier - The ending value to multiply against InValues
	 *  @param InAccumulateValues - The array which holds the sum.
	 */
	SIGNALPROCESSING_API void ArrayLerpAddInPlace(TArrayView<const float> InValues, float InStartMultiplier, float InEndMultiplier, TArrayView<float> InAccumulateValues);
	SIGNALPROCESSING_API void ArrayLerpAddInPlace(const AlignedFloatBuffer& InValues, float InStartMultiplier, float InEndMultiplier, AlignedFloatBuffer& InAccumulateValues);

	/** Subract arrays element-wise. OutArray = InMinuend - InSubtrahend
	 *
	 *  InMinuend is the array of data to be subtracted from.
	 *  InSubtrahend is the array of data to subtract.
	 *  OutArray is the array which holds the result.
	 */
	SIGNALPROCESSING_API void ArraySubtract(TArrayView<const float> InMinuend, TArrayView<const float> InSubtrahend, TArray<float>& OutArray);

	/** Subtract value from each element in InValues */
	SIGNALPROCESSING_API void ArraySubtractByConstantInPlace(TArrayView<float> InValues, float InSubtrahend);
	SIGNALPROCESSING_API void ArraySubtractByConstantInPlace(AlignedFloatBuffer& InValues, float InSubtrahend);


	/** Take Square Root of values in place. */
	SIGNALPROCESSING_API void ArraySqrtInPlace(TArrayView<float> InValues);


	/** Perform complex conjugate of array.  Assumes complex numbers are interlaves [real_0, imag_0, real_1, image_1, ..., real_N, imag_N]. */
	SIGNALPROCESSING_API void ArrayComplexConjugate(TArrayView<const float> InValues, TArrayView<float> OutValues);
	SIGNALPROCESSING_API void ArrayComplexConjugate(const AlignedFloatBuffer& InValues, AlignedFloatBuffer& OutValues);

	SIGNALPROCESSING_API void ArrayComplexConjugateInPlace(TArrayView<float> InValues);
	SIGNALPROCESSING_API void ArrayComplexConjugateInPlace(AlignedFloatBuffer& InValues);

	/** Convert magnitude values to decibel values in place. db = 20 * log10(val) */
	SIGNALPROCESSING_API void ArrayMagnitudeToDecibelInPlace(TArrayView<float> InValues, float InMinimumDb);
	SIGNALPROCESSING_API void ArrayMagnitudeToDecibelInPlace(AlignedFloatBuffer& InValues, float InMinimumDb);

	/** Convert power values to decibel values in place. db = 10 * log10(val) */
	SIGNALPROCESSING_API void ArrayPowerToDecibelInPlace(TArrayView<float> InValues, float InMinimumDb);
	SIGNALPROCESSING_API void ArrayPowerToDecibelInPlace(AlignedFloatBuffer& InValues, float InMinimumDb);


	/** Compute power of complex data. Out[i] = Complex[2 * i] * Complex[2 * i] + Complex[2 * i + 1] * Complex[2 * i + 1] */
	SIGNALPROCESSING_API void ArrayComplexToPower(TArrayView<float> InComplexSamples, TArray<float>& OutPowerSamples);

	/** Compute power of complex data. Out[i] = Complex[2 * i] * Complex[2 * i] + Complex[2 * i + 1] * Complex[2 * i + 1] */
	SIGNALPROCESSING_API void ArrayComplexToPower(const AlignedFloatBuffer& InComplexSamples, AlignedFloatBuffer& OutPowerSamples);



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
			int32 StartIndex = 0;
			TArray<float> OffsetValues;
		};

	public:
		FContiguousSparse2DKernelTransform(const FContiguousSparse2DKernelTransform& ) = delete;
		FContiguousSparse2DKernelTransform(const FContiguousSparse2DKernelTransform&& ) = delete;
		FContiguousSparse2DKernelTransform& operator=(const FContiguousSparse2DKernelTransform& ) = delete;

		/**
		 * NumInElements sets the expected number of input array elements as well as the number of elements in a row.
		 * NumOutElements sets the number of output array elements as well as the number or rows.
		 */
		FContiguousSparse2DKernelTransform(const int32 NumInElements, const int32 NumOutElements);
		virtual ~FContiguousSparse2DKernelTransform();

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
