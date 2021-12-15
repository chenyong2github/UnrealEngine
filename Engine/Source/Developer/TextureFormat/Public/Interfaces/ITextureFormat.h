// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PixelFormat.h"
#include "Serialization/CompactBinary.h"

struct FTextureBuildSettings;

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
	*	Return the name of the encoder used for the given format.
	* 
	*	Used for debugging and UI.
	* */
	virtual FName GetEncoderName(FName Format) const = 0;

	/** 
		Exposes whether the format supports the fast/final encode speed switching in project settings. 
		Needs the Format so that we can thunk through the child texture formats correctly.
	*/
	virtual bool SupportsEncodeSpeed(FName Format) const
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
		const FTextureBuildSettings* BuildSettings = nullptr
	) const = 0;

	/**
	 * Gets an optional derived data key string, so that the compressor can
	 * rely upon the number of mips, size of texture, etc, when compressing the image
	 *
	 * @param BuildSettings Reference to the build settings we are compressing with.
	 * @return A string that will be used with the DDC, the string should be in the format "<DATA>_"
	 */
	virtual FString GetDerivedDataKeyString(const FTextureBuildSettings& BuildSettings) const
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
	virtual FTextureFormatCompressorCaps GetFormatCapabilitiesEx(const FTextureBuildSettings& BuildSettings, uint32 NumMips, const struct FImage& ExampleImage, bool bImageHasAlphaChannel) const
	{
		return GetFormatCapabilities();
	}

	/**
	 * Calculate the final/runtime pixel format for this image on this platform
	 */
	virtual EPixelFormat GetPixelFormatForImage(const FTextureBuildSettings& BuildSettings, const struct FImage& Image, bool bImageHasAlphaChannel) const = 0;

	/**
	 * Compresses a single image.
	 *
	 * @param Image The input image.
	 * @param BuildSettings Build settings.
	 * @param DebugTexturePathName The path name of the texture we are building, for debug logging/filtering/dumping.
	 * @param bImageHasAlphaChannel true if the image has a non-white alpha channel.
	 * @param OutCompressedMip The compressed image.
	 * @returns true on success, false otherwise.
	 */
	virtual bool CompressImage(
		const struct FImage& Image,
		const FTextureBuildSettings& BuildSettings,
		FStringView DebugTexturePathName,
		bool bImageHasAlphaChannel,
		struct FCompressedImage2D& OutCompressedImage
	) const = 0;

	/**
	 * Compress an image (or images for a miptail) into a single mip blob.
	 *
	 * @param Images The input image(s)
	 * @param NumImages The number of images (for a miptail, this number should match what was returned in GetFormatCapabilities, mostly used for verification)
	 * @param BuildSettings Build settings.
	 * @param DebugTexturePathName The path name of the texture we are building, for debug logging/filtering/dumping.
	 * @param bImageHasAlphaChannel true if the image has a non-white alpha channel.
	 * @param ExtData Extra data that the format may want to have passed back in to each compress call (makes the format class be stateless)
	 * @param OutCompressedMip The compressed image.
	 * @returns true on success, false otherwise.
	 */
	virtual bool CompressImageEx(
		const struct FImage* Images,
		const uint32 NumImages,
		const FTextureBuildSettings& BuildSettings,
		FStringView DebugTexturePathName,
		bool bImageHasAlphaChannel,
		uint32 ExtData,
		FCompressedImage2D& OutCompressedImage) const
	{
		// general case can't handle mip tails
		if (Images == nullptr || NumImages > 1)
		{
			return false;
		}
		
		return CompressImage(*Images, BuildSettings, DebugTexturePathName, bImageHasAlphaChannel, OutCompressedImage);
	}

	/**
	 * An object produced by PrepareTiling and used by SetTiling and CompressImageTiled.
	 */
	struct FTilerSettings
	{
	};

	/**
	 * Compress an image (or images for a miptail) into a single mip blob with device-specific tiling.
	 *
	 * @param Image The input image.
	 * @param BuildSettings Build settings.
	 * @param bImageHasAlphaChannel true if the image has a non-white alpha channel.
	 * @param DebugTexturePathName The path name of the texture we are building, for debug logging/filtering/dumping.
	 * @param OutCompressedMip The compressed image.
	 * @param Tiler The tiler settings.
	 * @returns true on success, false otherwise.
	 */
	virtual bool CompressImageTiled(
		const struct FImage* Images,
		uint32 NumImages,
		const FTextureBuildSettings& BuildSettings,
		FStringView DebugTexturePathName,
		bool bImageHasAlphaChannel,
		TSharedPtr<FTilerSettings>& TilerSettings,
		struct FCompressedImage2D& OutCompressedImage) const
	{
		unimplemented();
		return false;
	}

	/**
	 * Whether device-specific tiling is supported by the compressor.
	 *
	 * @param BuildSettings Build settings.
	 * @returns true if tiling is supported, false if it must be done by the caller
	 *
	 */
	virtual bool SupportsTiling(const FTextureBuildSettings& BuildSettings) const
	{
		return false;
	}

	/**
	 * Prepares to compresses a single image with tiling. The result OutTilerSettings is used by SetTiling and CompressImageTiled.
	 *
	 * @param Image The input image.
	 * @param BuildSettings Build settings.
	 * @param bImageHasAlphaChannel true if the image has a non-white alpha channel.
	 * @param OutTilerSettings The tiler settings that will be used by CompressImageTiled and SetTiling.
	 * @param OutCompressedImage The image to tile.
	 * @returns true on success, false otherwise.
	 */
	virtual bool PrepareTiling(
		const FImage* Images,
		const uint32 NumImages,
		const FTextureBuildSettings& BuildSettings,
		bool bImageHasAlphaChannel,
		TSharedPtr<FTilerSettings>& OutTilerSettings,
		TArray<FCompressedImage2D>& OutCompressedImage
	) const
	{
		unimplemented();
		return false;
	}
	
	/**
	 * Sets the tiling settings after device-specific tiling has been performed.
	 *
	 * @param BuildSettings Build settings.
	 * @param TilerSettings The tiler settings produced by PrepareTiling.
	 * @param ReorderedBlocks The blocks that have been tiled.
	 * @param NumBlocks The number of blocks.
	 * @returns true on success, false otherwise.
	 */
	virtual bool SetTiling(
		const FTextureBuildSettings& BuildSettings,
		TSharedPtr<FTilerSettings>& TilerSettings,
		const TArray64<uint8>& ReorderedBlocks,
		uint32 NumBlocks
	) const
	{
		unimplemented();
		return false;
	}

	/**
	 * Cleans up the FTilerSettings object once it is finished.
	 * 
	 * @param BuildSettings Build settings.
	 * @param TilerSettings The tiler settings object to release.
	 */
	virtual void ReleaseTiling(const FTextureBuildSettings& BuildSettings, TSharedPtr<FTilerSettings>& TilerSettings) const
	{
		unimplemented();
	}

	/**
	 * Obtains the current global format config object for this texture format.
	 * 
	 * This is only ever called during task creation - never in a build worker
	 * (FormatConfigOverride is empty)
	 * 
	 * @param BuildSettings Build settings.
	 * @returns The current format config object or an empty object if no format config is defined for this texture format.
	 */
	virtual FCbObject ExportGlobalFormatConfig(const FTextureBuildSettings& BuildSettings) const
	{
		return FCbObject();
	}

	/**
	 * If this is an Alternate Texture Format, return the prefix to apply 
	 */
	virtual FString GetAlternateTextureFormatPrefix() const
	{
		return FString();
	}
	
	/**
	 * Identify the latest sdk version for this texture encoder
	 *   (note the SdkVersion is different than the TextureFormat Version)
	 */
	virtual FName GetLatestSdkVersion() const
	{
		return FName();
	}
	
	UE_DEPRECATED(5.0, "Legacy API - do not use")
	virtual bool UsesTaskGraph() const
	{
		unimplemented();
		return true;
	}

public:

	/** Virtual destructor. */
	virtual ~ITextureFormat() { }
};
