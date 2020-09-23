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
			// if this fails to load, there's a logic error, so use Checked
			ITextureFormat* BaseFormat = FModuleManager::LoadModuleChecked<ITextureFormatModule>(ModuleName).GetTextureFormat();
			BaseFormat->GetSupportedFormats(BaseFormats);
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

	virtual uint16 GetVersion(FName Format, const struct FTextureBuildSettings* BuildSettings) const override
	{
		uint16 BaseVersion = GetBaseFormatObject(Format)->GetVersion(Format, BuildSettings);
		checkf(BaseVersion < 256, TEXT("BaseFormat for %s had too large a version (%d), must fit in 8bits"), *Format.ToString(), BaseVersion);

		uint8 ChildVersion = GetChildFormatVersion(Format, BuildSettings);

		// 8 bits for each version
		return (BaseVersion << 8) | ChildVersion;
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


protected:

	// Prefix put before all formats from parent formats
	FString FormatPrefix;

	// List of base formats that. Combined with FormatPrefix, this contains all formats this can handle
	TArray<FName> BaseFormats;
};
