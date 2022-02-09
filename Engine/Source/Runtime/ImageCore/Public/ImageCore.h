// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


/* Dependencies
 *****************************************************************************/

#include "CoreMinimal.h"

/* Types
 *****************************************************************************/

namespace ERawImageFormat
{
	/**
	 * Enumerates supported raw image formats.
	 */
	enum Type : uint8
	{
		G8,
		BGRA8,
		BGRE8,
		RGBA16,
		RGBA16F,
		RGBA32F,
		G16,
		R16F,
	};
};

namespace UE {
	namespace Color {
		enum class EChromaticAdaptationMethod : uint8;
	}
}


/**
 * Structure for raw image data.
 */
struct FImage
{
	/** Raw image data. */
	TArray64<uint8> RawData;

	/** Width of the image. */
	int32 SizeX;
	
	/** Height of the image. */
	int32 SizeY;
	
	/** Number of image slices. */
	int32 NumSlices;
	
	/** Format in which the image is stored. */
	ERawImageFormat::Type Format;
	
	/** The gamma space the image is stored in. */
	EGammaSpace GammaSpace;

public:

	/**
	 * Default constructor.
	 */
	FImage() { }

	/**
	 * Creates and initializes a new image with the specified number of slices.
	 *
	 * @param InSizeX - The image width.
	 * @param InSizeY - The image height.
	 * @param InNumSlices - The number of slices.
	 * @param InFormat - The image format.
	 * @param bInSRGB - Whether the color values are in SRGB format.
	 */
	IMAGECORE_API FImage(int32 InSizeX, int32 InSizeY, int32 InNumSlices, ERawImageFormat::Type InFormat, EGammaSpace InGammaSpace = EGammaSpace::Linear);

	/**
	 * Creates and initializes a new image with a single slice.
	 *
	 * @param InSizeX - The image width.
	 * @param InSizeY - The image height.
	 * @param InFormat - The image format.
	 * @param bInSRGB - Whether the color values are in SRGB format.
	 */
	IMAGECORE_API FImage(int32 InSizeX, int32 InSizeY, ERawImageFormat::Type InFormat, EGammaSpace InGammaSpace = EGammaSpace::Linear);


public:

	/**
	 * Copies the image to a destination image with the specified format.
	 *
	 * @param DestImage - The destination image.
	 * @param DestFormat - The destination image format.
	 * @param DestSRGB - Whether the destination image is in SRGB format.
	 */
	IMAGECORE_API void CopyTo(FImage& DestImage, ERawImageFormat::Type DestFormat, EGammaSpace DestGammaSpace) const;

	/**
	 * Copies and resizes the image to a destination image with the specified size and format.
	 * Resize is done using bilinear filtering
	 *
	 * @param DestImage - The destination image.
	 * @param DestSizeX - Width of the resized image
	 * @param DestSizeY - Height of the resized image
	 * @param DestFormat - The destination image format.
	 * @param DestSRGB - Whether the destination image is in SRGB format.
	 */
	IMAGECORE_API void ResizeTo(FImage& DestImage, int32 DestSizeX, int32 DestSizeY, ERawImageFormat::Type DestFormat, EGammaSpace DestGammaSpace) const;

	/**
	 * Linearize to a RGBA32F destination image by applying the decoding function that corresponds to the specified source encoding.
	 * If None, this call will be equivalent to CopyTo(DestImage, ERawImageFormat::RGBA32F, EGammaSpace::Linear).
	 *
	 * @param SourceEncoding - Opaque source encoding (matching UE::Color::EEncoding).
	 * @param DestImage - The destination image.
	 */
	IMAGECORE_API void Linearize(uint8 SourceEncoding, FImage& DestImage) const;

	/**
	 * Apply a color space transformation from the source chromaticities to the engine's working color space.
	 *
	 * @param SourceRedChromaticity - The red chromaticity coordinate of the source color space.
	 * @param SourceGreenChromaticity - The green chromaticity coordinate of the source color space.
	 * @param SourceBlueChromaticity - The blue chromaticity coordinate of the source color space.
	 * @param SourceWhiteChromaticity - The white chromaticity coordinate of the source color space.
	 * @param Method - The chromatic adapation method.
	 * @param EqualityTolerance - The tolerance for the source and working color space chromaticities to be considered equal, bypassing the transform.
	 */
	IMAGECORE_API void TransformToWorkingColorSpace(const FVector2D& SourceRedChromaticity, const FVector2D& SourceGreenChromaticity, const FVector2D& SourceBlueChromaticity, const FVector2D& SourceWhiteChromaticity, UE::Color::EChromaticAdaptationMethod Method, double EqualityTolerance = 1.e-7);

	/**
	 * Gets the number of bytes per pixel.
	 *
	 * @return Bytes per pixel.
	 */
	IMAGECORE_API int32 GetBytesPerPixel() const;

	/**
	 * Initializes this image with the specified number of slices.
	 *
	 * @param InSizeX - The image width.
	 * @param InSizeY - The image height.
	 * @param InNumSlices - The number of slices.
	 * @param InFormat - The image format.
	 * @param bInSRGB - Whether the color values are in SRGB format.
	 */
	IMAGECORE_API void Init(int32 InSizeX, int32 InSizeY, int32 InNumSlices, ERawImageFormat::Type InFormat, EGammaSpace InGammaSpace = EGammaSpace::Linear);

	/**
	 * Initializes this image with a single slice.
	 *
	 * @param InSizeX - The image width.
	 * @param InSizeY - The image height.
	 * @param InFormat - The image format.
	 * @param bInSRGB - Whether the color values are in SRGB format.
	 */
	IMAGECORE_API void Init(int32 InSizeX, int32 InSizeY, ERawImageFormat::Type InFormat, EGammaSpace InGammaSpace = EGammaSpace::Linear);


public:

	// Convenience accessors to raw data
	
	TArrayView64<uint8> AsG8()
	{
		check(Format == ERawImageFormat::G8);
		return { RawData.GetData(), int64(RawData.Num()) };
	}

	TArrayView64<uint16> AsG16()
	{
		check(Format == ERawImageFormat::G16);
		return { (uint16*)RawData.GetData(), int64(RawData.Num() / sizeof(uint16)) };
	}

	TArrayView64<FColor> AsBGRA8()
	{
		check(Format == ERawImageFormat::BGRA8);
		return { (struct FColor*)RawData.GetData(), int64(RawData.Num() / sizeof(FColor)) };
	}

	TArrayView64<FColor> AsBGRE8()
	{
		check(Format == ERawImageFormat::BGRE8);
		return { (struct FColor*)RawData.GetData(), int64(RawData.Num() / sizeof(FColor)) };
	}

	TArrayView64<uint16> AsRGBA16()
	{
		check(Format == ERawImageFormat::RGBA16);
		return { (uint16*)RawData.GetData(), int64(RawData.Num() / sizeof(uint16)) };
	}

	TArrayView64<FFloat16Color> AsRGBA16F()
	{
		check(Format == ERawImageFormat::RGBA16F);
		return { (class FFloat16Color*)RawData.GetData(), int64(RawData.Num() / sizeof(FFloat16Color)) };
	}

	TArrayView64<FLinearColor> AsRGBA32F()
	{
		check(Format == ERawImageFormat::RGBA32F);
		return { (struct FLinearColor*)RawData.GetData(), int64(RawData.Num() / sizeof(FLinearColor)) };
	}

	TArrayView64<FFloat16> AsR16F()
	{
		check(Format == ERawImageFormat::R16F);
		return { (class FFloat16*)RawData.GetData(), int64(RawData.Num() / sizeof(FFloat16)) };
	}

	// Convenience accessors to const raw data

	TArrayView64<const uint8> AsG8() const
	{
		check(Format == ERawImageFormat::G8);
		return { RawData.GetData(), int64(RawData.Num()) };
	}

	TArrayView64<const uint16> AsG16() const
	{
		check(Format == ERawImageFormat::G16);
		return { (const uint16*)RawData.GetData(), int64(RawData.Num() / sizeof(uint16)) };
	}

	TArrayView64<const FColor> AsBGRA8() const
	{
		check(Format == ERawImageFormat::BGRA8);
		return { (const struct FColor*)RawData.GetData(), int64(RawData.Num() / sizeof(FColor)) };
	}

	TArrayView64<const FColor> AsBGRE8() const
	{
		check(Format == ERawImageFormat::BGRE8);
		return { (struct FColor*)RawData.GetData(), int64(RawData.Num() / sizeof(FColor)) };
	}

	TArrayView64<const uint16> AsRGBA16() const
	{
		check(Format == ERawImageFormat::RGBA16);
		return { (const uint16*)RawData.GetData(), int64(RawData.Num() / sizeof(uint16)) };
	}

	TArrayView64<const FFloat16Color> AsRGBA16F() const
	{
		check(Format == ERawImageFormat::RGBA16F);
		return { (const class FFloat16Color*)RawData.GetData(), int64(RawData.Num() / sizeof(FFloat16Color)) };
	}

	TArrayView64<const FLinearColor> AsRGBA32F() const
	{
		check(Format == ERawImageFormat::RGBA32F);
		return { (struct FLinearColor*)RawData.GetData(), int64(RawData.Num() / sizeof(FLinearColor)) };
	}

	TArrayView64<const FFloat16> AsR16F() const
	{
		check(Format == ERawImageFormat::R16F);
		return { (const class FFloat16*)RawData.GetData(), int64(RawData.Num() / sizeof(FFloat16)) };
	}

	FORCEINLINE bool IsGammaCorrected() const
	{
		return GammaSpace != EGammaSpace::Linear;
	}
};


IMAGECORE_API int32 ImageParallelForComputeNumJobsForPixels(int64 & OutNumPixelsPerJob,int64 NumPixels);

IMAGECORE_API int32 ImageParallelForComputeNumJobsForRows(int32 & OutNumItemsPerJob,int32 SizeX,int32 SizeY);