// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/ITextureFormat.h"
#include "Interfaces/ITextureFormatModule.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "TextureCompressorModule.h"

/**
 * Version of ITextureFormat that handles a child texture format that is used as a "post-process" after compressing textures, useful for
 * several platforms that need to modify already compressed texture data for optimal data
 */
class FChildTextureFormat : public ITextureFormat
{
public:

	FChildTextureFormat(const TCHAR* PlatformFormatPrefix)
		: FormatPrefix(PlatformFormatPrefix)
	{

	}

protected:
	void AddBaseTextureFormatModules(const TCHAR* ModuleNameWildcard)
	{
		TArray<FName> Modules;
		FModuleManager::Get().FindModules(ModuleNameWildcard, Modules);

		for (FName ModuleName : Modules)
		{
			ITextureFormatModule * TFModule = FModuleManager::LoadModulePtr<ITextureFormatModule>(ModuleName);
			if ( TFModule != nullptr )
			{
				ITextureFormat* BaseFormat = TFModule->GetTextureFormat();
				BaseFormat->GetSupportedFormats(BaseFormats);
			}
		}
	}

	FName GetBaseFormatName(FName PlatformName) const
	{
		return FName(*(PlatformName.ToString().Replace(*FormatPrefix, TEXT(""))));
	}

	/**
	 * Given a platform specific format name, get the parent texture format object
	 */
	const ITextureFormat* GetBaseFormatObject(FName PlatformName) const
	{
		FName BaseFormatName = GetBaseFormatName(PlatformName);

		ITargetPlatformManagerModule& TPM = FModuleManager::LoadModuleChecked<ITargetPlatformManagerModule>("TargetPlatform");
		const ITextureFormat* FormatObject = TPM.FindTextureFormat(BaseFormatName);

		checkf(FormatObject != nullptr, TEXT("Bad PlatformName %s passed to FChildTextureFormat::GetBaseFormatObject()"));

		return FormatObject;
	}

	/**
	 * The final version is a combination of parent and child formats, 8 bits for each
	 */
	virtual uint8 GetChildFormatVersion(FName Format, const struct FTextureBuildSettings* BuildSettings) const = 0;

	/**
	 * Make the child type think about if they need a key string or not, by making it pure virtual
	 */
	virtual FString GetChildDerivedDataKeyString(const class UTexture& Texture, const FTextureBuildSettings* BuildSettings) const = 0;


public:

	//// ITextureFormat interface ////

	virtual void GetSupportedFormats(TArray<FName>& OutFormats) const override
	{
		for (FName BaseFormat : BaseFormats)
		{
			FName ChildFormat(*(FormatPrefix + BaseFormat.ToString()));
			OutFormats.Add(ChildFormat);
		}
	}

	virtual uint16 GetVersion(FName Format, const struct FTextureBuildSettings* BuildSettings) const final
	{
		uint16 BaseVersion = GetBaseFormatObject(Format)->GetVersion(Format, BuildSettings);
		checkf(BaseVersion < 256, TEXT("BaseFormat for %s had too large a version (%d), must fit in 8bits"), *Format.ToString(), BaseVersion);

		uint8 ChildVersion = GetChildFormatVersion(Format, BuildSettings);

		// 8 bits for each version
		return (BaseVersion << 8) | ChildVersion;
	}

	virtual FString GetDerivedDataKeyString(const class UTexture& Texture, const FTextureBuildSettings* BuildSettings) const final
	{
		FTextureBuildSettings BaseSettings = *BuildSettings;
		BaseSettings.TextureFormatName = GetBaseFormatName(BuildSettings->TextureFormatName);

		FString BaseString = GetBaseFormatObject(BuildSettings->TextureFormatName)->GetDerivedDataKeyString(Texture, &BaseSettings);
		FString ChildString = GetChildDerivedDataKeyString(Texture, BuildSettings);

		return BaseString + ChildString;
	}

	virtual EPixelFormat GetPixelFormatForImage(const struct FTextureBuildSettings& BuildSettings, const struct FImage& ExampleImage, bool bImageHasAlphaChannel) const override 
	{
		FTextureBuildSettings Settings = BuildSettings;
		Settings.TextureFormatName = GetBaseFormatName(BuildSettings.TextureFormatName);
		return GetBaseFormatObject(BuildSettings.TextureFormatName)->GetPixelFormatForImage(Settings, ExampleImage, bImageHasAlphaChannel);
	}

	bool CompressBaseImage(
		const FImage& InImage,
		const struct FTextureBuildSettings& BuildSettings,
		bool bImageHasAlphaChannel,
		FCompressedImage2D& OutCompressedImage
	) const
	{
		FTextureBuildSettings BaseSettings = BuildSettings;
		BaseSettings.TextureFormatName = GetBaseFormatName(BuildSettings.TextureFormatName);

		// pass along the compression to the base format
		if (GetBaseFormatObject(BuildSettings.TextureFormatName)->CompressImage(InImage, BaseSettings, bImageHasAlphaChannel, OutCompressedImage) == false)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to compress with base compressor [format %s]"), *BaseSettings.TextureFormatName.ToString());
			return false;
		}
		return true;
	}

	bool CompressBaseImageTiled(
		const FImage* Images,
		uint32 NumImages,
		const struct FTextureBuildSettings& BuildSettings,
		bool bImageHasAlphaChannel,
		TSharedPtr<FTilerSettings>& TilerSettings,
		FCompressedImage2D& OutCompressedImage
	) const
	{
		FTextureBuildSettings BaseSettings = BuildSettings;
		BaseSettings.TextureFormatName = GetBaseFormatName(BuildSettings.TextureFormatName);

		// pass along the compression to the base format
		if (GetBaseFormatObject(BuildSettings.TextureFormatName)->CompressImageTiled(Images, NumImages, BaseSettings, bImageHasAlphaChannel, TilerSettings, OutCompressedImage) == false)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to compress with base tiled compressor [format %s]"), *BaseSettings.TextureFormatName.ToString());
			return false;
		}
		return true;
	}

	bool PrepareTiling(
		const FImage* Images,
		const uint32 NumImages,
		const struct FTextureBuildSettings& BuildSettings,
		bool bImageHasAlphaChannel,
		TSharedPtr<FTilerSettings>& OutTilerSettings,
		TArray<FCompressedImage2D>& OutCompressedImages
	) const override
	{
		FTextureBuildSettings BaseSettings = BuildSettings;
		BaseSettings.TextureFormatName = GetBaseFormatName(BuildSettings.TextureFormatName);

		return GetBaseFormatObject(BuildSettings.TextureFormatName)->PrepareTiling(Images, NumImages, BaseSettings, bImageHasAlphaChannel, OutTilerSettings, OutCompressedImages);
	}

	bool SetTiling(
		const struct FTextureBuildSettings& BuildSettings,
		TSharedPtr<FTilerSettings>& TilerSettings,
		const TArray64<uint8>& ReorderedBlocks,
		uint32 NumBlocks
	) const override
	{
		FTextureBuildSettings BaseSettings = BuildSettings;
		BaseSettings.TextureFormatName = GetBaseFormatName(BuildSettings.TextureFormatName);

		return GetBaseFormatObject(BuildSettings.TextureFormatName)->SetTiling(BaseSettings, TilerSettings, ReorderedBlocks, NumBlocks);
	}

	void ReleaseTiling(const struct FTextureBuildSettings& BuildSettings, TSharedPtr<FTilerSettings>& TilerSettings) const override
	{
		FTextureBuildSettings BaseSettings = BuildSettings;
		BaseSettings.TextureFormatName = GetBaseFormatName(BuildSettings.TextureFormatName);

		return GetBaseFormatObject(BuildSettings.TextureFormatName)->ReleaseTiling(BuildSettings, TilerSettings);
	}

protected:

	// Prefix put before all formats from parent formats
	FString FormatPrefix;

	// List of base formats that. Combined with FormatPrefix, this contains all formats this can handle
	TArray<FName> BaseFormats;
};
