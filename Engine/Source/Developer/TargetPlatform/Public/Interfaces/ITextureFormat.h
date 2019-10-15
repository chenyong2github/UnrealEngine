// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Structure for texture format compressor capabilities.
 */
struct FTextureFormatCompressorCaps
{
	FTextureFormatCompressorCaps()
		: MaxTextureDimension(TNumericLimits<uint32>::Max())
		, NumMipsInTail(0)
		, ExtData(0)
	{ }

	uint32 MaxTextureDimension;
	uint32 NumMipsInTail;
	uint32 ExtData;
};


/**
 * Interface for texture compression modules.
 */
class ITextureFormat
{
public:

	/**
	 * Checks whether this texture format can compress in parallel.
	 *
	 * @return true if parallel compression is supported, false otherwise.
	 */
	virtual bool AllowParallelBuild() const
	{
		return false;
	}

	/**
	 * Gets the current version of the specified texture format.
	 *
	 * @param Format The format to get the version for.
	 * @return Version number.
	 */
	virtual uint16 GetVersion(
		FName Format,
		const struct FTextureBuildSettings* BuildSettings = nullptr
	) const = 0;

	/**
	 * Gets an optional derived data key string, so that the compressor can
	 * rely upon the number of mips, size of texture, etc, when compressing the image
	 *
	 * @param Texture Reference to the texture we are compressing.
	 * @return A string that will be used with the DDC, the string should be in the format "<DATA>_"
	 */
	virtual FString GetDerivedDataKeyString( const class UTexture& Texture ) const
	{
		return TEXT("");
	}

	/**
	 * Gets the list of supported formats.
	 *
	 * @param OutFormats Will hold the list of formats.
	 */
	virtual void GetSupportedFormats( TArray<FName>& OutFormats ) const = 0;

	/**
	* Gets the capabilities of the texture compressor.
	*
	* @param OutCaps Filled with capability properties of texture format compressor.
	*/
	virtual FTextureFormatCompressorCaps GetFormatCapabilities() const = 0;

	/**
	* Gets the capabilities of the texture compressor.
	*
	* @param OutCaps Filled with capability properties of texture format compressor.
	*/
	virtual FTextureFormatCompressorCaps GetFormatCapabilitiesEx(const struct FTextureBuildSettings& BuildSettings, uint32 NumMips, const struct FImage& ExampleImage) const
	{
		return GetFormatCapabilities();
	}

	/**
	 * Compresses a single image.
	 *
	 * @param Image The input image.
	 * @param BuildSettings Build settings.
	 * @param bImageHasAlphaChannel true if the image has a non-white alpha channel.
	 * @param OutCompressedMip The compressed image.
	 * @returns true on success, false otherwise.
	 */
	virtual bool CompressImage(
		const struct FImage& Image,
		const struct FTextureBuildSettings& BuildSettings,
		bool bImageHasAlphaChannel,
		struct FCompressedImage2D& OutCompressedImage
	) const = 0;

	/**
	 * Compress an image (or images for a miptail) into a single mip blob
	 *
	 * @param Images The input image(s)
	 * @param Images The number of images (for a miptail, this number should match what was returned in GetFormatCapabilities, mostly used for verification)
	 * @param BuildSettings Build settings.
	 * @param bImageHasAlphaChannel true if the image has a non-white alpha channel.
	 * @param ExtData Extra data that the format may want to have passed back in to each compress call (makes the format class be stateless)
	 * @param OutCompressedMip The compressed image.
	 * @returns true on success, false otherwise.
	 */
	virtual bool CompressImageEx(const struct FImage* Images,
		const uint32 NumImages,
		const struct FTextureBuildSettings& BuildSettings,
		bool bImageHasAlphaChannel,
		uint32 ExtData,
		FCompressedImage2D& OutCompressedImage) const
	{
		// general case can't handle mip tails
		if (Images == nullptr || NumImages > 1)
		{
			return false;
		}
		
		return CompressImage(*Images, BuildSettings, bImageHasAlphaChannel, OutCompressedImage);
	}

public:

	/** Virtual destructor. */
	virtual ~ITextureFormat() { }
};
