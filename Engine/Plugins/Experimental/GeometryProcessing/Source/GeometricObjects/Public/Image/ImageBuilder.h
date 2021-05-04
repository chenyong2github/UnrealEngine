// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MathUtil.h"
#include "VectorTypes.h"
#include "Image/ImageDimensions.h"
#include "Spatial/DenseGrid2.h"

namespace UE
{
namespace Geometry
{

/**
 * TImageBuilder is used to create and populate a 2D image with a templated "pixel" type.
 */
template<typename PixelType>
class TImageBuilder
{
protected:
	FImageDimensions Dimensions;
	TDenseGrid2<PixelType> Image;

public:

	void SetDimensions(FImageDimensions DimensionsIn)
	{
		Dimensions = DimensionsIn;
		Image.Resize(Dimensions.GetWidth(), Dimensions.GetHeight(), true);
	}

	const FImageDimensions& GetDimensions() const
	{
		return Dimensions;
	}

	/**
	 * Clear all Pixels in the current Mip to the clear/default color for the texture build type
	 */
	void Clear(const PixelType& ClearValue)
	{
		Image.AssignAll(ClearValue);
	}

	/**
	 * @return true if the given X/Y coordinates are inside the image pixel bounds, ie can be used to index a pixel
	 */
	bool ContainsPixel(int32 X, int32 Y) const
	{
		return X >= 0 && Y >= 0 && X < Dimensions.GetWidth() && Y < Dimensions.GetHeight();
	}

	/**
	 * @return true if the given X/Y coordinates are inside the image pixel bounds, ie can be used to index a pixel
	 */
	bool ContainsPixel(const FVector2i& ImageCoords) const
	{
		return ImageCoords.X >= 0 && ImageCoords.Y >= 0 &&
			ImageCoords.X < Dimensions.GetWidth() && ImageCoords.Y < Dimensions.GetHeight();
	}

	/**
	 * Get the Pixel at the given X/Y coordinates
	 */
	const PixelType& GetPixel(int32 X, int32 Y) const
	{
		int64 LinearIndex = Dimensions.GetIndex(FVector2i(X,Y));
		return Image[LinearIndex];
	}

	/**
	 * Get the Pixel at the given X/Y coordinates
	 */
	const PixelType& GetPixel(const FVector2i& ImageCoords) const
	{
		int64 LinearIndex = Dimensions.GetIndex(ImageCoords);
		return Image[LinearIndex];
	}

	/**
	 * Get the Pixel at the given linear index
	 */
	const PixelType& GetPixel(int64 LinearIndex) const
	{
		return Image[LinearIndex];
	}


	/**
	 * Set the Pixel at the given X/Y coordinates to the given PixelType
	 */
	void SetPixel(int32 X, int32 Y, const PixelType& NewValue)
	{
		int64 LinearIndex = Dimensions.GetIndex(FVector2i(X,Y));
		Image[LinearIndex] = NewValue;
	}

	/**
	 * Set the Pixel at the given X/Y coordinates to the given PixelType
	 */
	void SetPixel(const FVector2i& ImageCoords, const PixelType& NewValue)
	{
		int64 LinearIndex = Dimensions.GetIndex(ImageCoords);
		Image[LinearIndex] = NewValue;
	}


	/**
	 * Set the Pixel at the given linear index to the given PixelType
	 */
	void SetPixel(int64 LinearIndex, const PixelType& NewValue)
	{
		Image[LinearIndex] = NewValue;
	}


	/**
	 * Copy Pixel value from one linear index to another
	 */
	void CopyPixel(int64 FromLinearIndex, int64 ToLinearIndex)
	{
		Image[ToLinearIndex] = Image[FromLinearIndex];
	}


	/**
	 * Convert to a different data type of same Dimensions using ConvertFunc
	 */
	template<typename OtherType>
	void Convert(TFunctionRef<OtherType(const PixelType&)> ConvertFunc,
				 TImageBuilder<OtherType>& ConvertedImageOut) const
	{
		ConvertedImageOut.SetDimensions(Dimensions);
		int64 NumPixels = Dimensions.Num();
		for (int64 k = 0; k < NumPixels; ++k)
		{
			ConvertedImageOut.Image[k] = ConvertFunc(Image[k]);
		}
	}


	/**
	 * Sample the image value at floating-point pixel coords with Bilinear interpolation
	 */
	template<typename ScalarType>
	PixelType BilinearSample(const FVector2d& PixelCoords, const PixelType& InvalidValue) const
	{
		double X = PixelCoords.X;
		double Y = PixelCoords.Y;

		int X0 = (int)X, X1 = X0 + 1;
		int Y0 = (int)Y, Y1 = Y0 + 1;

		// make sure we are in range
		if (X0 < 0 || X1 >= Dimensions.GetWidth() ||
			Y0 < 0 || Y1 >= Dimensions.GetHeight())
		{
			return InvalidValue;
		}

		// convert double coords to [0,1] range
		double Ax = PixelCoords.X - (double)X0;
		double Ay = PixelCoords.Y - (double)Y0;
		double OneMinusAx = 1.0 - Ax;
		double OneMinusAy = 1.0 - Ay;

		PixelType V00 = GetPixel(FVector2i(X0, Y0));
		PixelType V10 = GetPixel(FVector2i(X1, Y0));
		PixelType V01 = GetPixel(FVector2i(X0, Y1));
		PixelType V11 = GetPixel(FVector2i(X1, Y1));

		return V00 * (ScalarType)(OneMinusAx * OneMinusAy) +
			   V01 * (ScalarType)(OneMinusAx * Ay) +
			   V10 * (ScalarType)(Ax * OneMinusAy) +
			   V11 * (ScalarType)(Ax * Ay);
	}


	/**
	 * Sample the image value at floating-point UV coords with Bilinear interpolation.
	 * The UV coords are assumed to be in range [0,1]x[0,1], and that this maps to the [0,Width]x[0,Height] image pixel rectangle.
	 */
	template<typename ScalarType>
	PixelType BilinearSampleUV(const FVector2d& UVCoords, const PixelType& InvalidValue) const
	{
		FVector2d PixelCoords(
			UVCoords.X * (double)Dimensions.GetWidth(),
			UVCoords.Y * (double)Dimensions.GetHeight());

		return BilinearSample<ScalarType>(PixelCoords, InvalidValue);
	}

	/**
	 * Very basic downsampling technqiue that just averages NxN pixel blocks. Multi-threaded.
	 * @param SubSteps each NxN pixel block of this size is averaged into 1 pixel in the output image
	 * @param Zero value to use with template PixelType must be provided, pixel values will be added to this value
	 * @param AverageFunc Called with Sum(Pixels) and PixelCount, return value is used as new pixel value (eg should be average)
	 */
	TImageBuilder<PixelType> FastDownsample(
		int32 SubSteps, 
		const PixelType& ZeroValue, 
		TFunctionRef<PixelType(PixelType, int)> AverageFunc) const
	{
		TImageBuilder<PixelType> DownsampleImage;
		int32 Width = Dimensions.GetWidth();
		int32 Height = Dimensions.GetHeight();

		// can only fast-downsample by even multiple of image size
		if (ensure((Width % SubSteps == 0) && (Height % SubSteps == 0)) == false)
		{
			DownsampleImage = *this;
			return DownsampleImage;
		}

		int32 SubWidth = Width / SubSteps;
		int32 SubHeight = Height / SubSteps;

		FImageDimensions SubDimensions(SubWidth, SubHeight);
		DownsampleImage.SetDimensions(SubDimensions);

		ParallelFor(SubHeight, [&](int32 yi)
		{
			int32 baseyi = yi * SubSteps;
			for (int32 xi = 0; xi < SubWidth; ++xi)
			{
				int32 basexi = xi * SubSteps;

				PixelType AccumBasePixels = ZeroValue;
				for (int32 dy = 0; dy < SubSteps; ++dy)
				{
					for (int32 dx = 0; dx < SubSteps; ++dx)
					{
						AccumBasePixels += GetPixel(basexi + dx, baseyi + dy);
					}
				}
				PixelType SubPixel = AverageFunc(AccumBasePixels, SubSteps * SubSteps);
				DownsampleImage.SetPixel(xi, yi, SubPixel);
			}
		});

		return DownsampleImage;
	}
	

};


} // end namespace UE::Geometry
} // end namespace UE