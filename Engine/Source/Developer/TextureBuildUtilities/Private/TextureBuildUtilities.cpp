// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureBuildUtilities.h"
#include "Misc/DataDrivenPlatformInfoRegistry.h"


namespace UE
{
namespace TextureBuildUtilities
{

// Return true if texture format name is HDR
TEXTUREBUILDUTILITIES_API bool TextureFormatIsHdr(FName const& InName)
{
	// TextureFormatRemovePrefixFromName first !
	
	static FName NameRGBA16F(TEXT("RGBA16F"));
	static FName NameRGBA32F(TEXT("RGBA32F"));
	static FName NameR16F(TEXT("R16F"));
	static FName NameR32F(TEXT("R32F"));
	static FName NameBC6H(TEXT("BC6H"));

	if ( InName == NameRGBA16F ) return true;
	if ( InName == NameRGBA32F ) return true;
	if ( InName == NameR16F ) return true;
	if ( InName == NameR32F ) return true;
	if ( InName == NameBC6H ) return true;

	return false;
}

TEXTUREBUILDUTILITIES_API const FName TextureFormatRemovePrefixFromName(FName const& InName, FName& OutPrefix)
{
	FString NameString = InName.ToString();

	// Format names may have one of the following forms:
	// - PLATFORM_PREFIX_FORMAT
	// - PLATFORM_FORMAT
	// - PREFIX_FORMAT
	// - FORMAT
	// We have to remove the platform prefix first, if it exists.
	// Then we detect a non-platform prefix (such as codec name)
	// and split the result into  explicit FORMAT and PREFIX parts.

	for (FName PlatformName : FDataDrivenPlatformInfoRegistry::GetSortedPlatformNames(EPlatformInfoType::AllPlatformInfos))
	{
		FString PlatformTextureFormatPrefix = PlatformName.ToString();
		PlatformTextureFormatPrefix += TEXT('_');
		if (NameString.StartsWith(PlatformTextureFormatPrefix, ESearchCase::IgnoreCase))
		{
			// Remove platform prefix and proceed with non-platform prefix detection.
			NameString = NameString.RightChop(PlatformTextureFormatPrefix.Len());
			break;
		}
	}

	int32 UnderscoreIndex = INDEX_NONE;
	if (NameString.FindChar(TCHAR('_'), UnderscoreIndex))
	{
		// Non-platform prefix, we want to keep these
		OutPrefix = *NameString.Left(UnderscoreIndex + 1);
		return *NameString.RightChop(UnderscoreIndex + 1);
	}

	return *NameString;
}


TEXTUREBUILDUTILITIES_API ERawImageFormat::Type GetVirtualTextureBuildIntermediateFormat(const FTextureBuildSettings& BuildSettings)
{
	FName TextureFormatPrefix;
	const FName TextureFormatName = TextureFormatRemovePrefixFromName(BuildSettings.TextureFormatName, TextureFormatPrefix);

	// @todo Oodle: @@!! bIsHdr shouldn't be looking at HDRSource !
	//	FIX ME beware possible changes to output; have to verify it's a nop on real data
	const bool bIsHdr = BuildSettings.bHDRSource || TextureFormatIsHdr(TextureFormatName);

	if (bIsHdr)
	{
		return ERawImageFormat::RGBA16F;
	}
	else if ( TextureFormatName == "G16" )
	{
		return ERawImageFormat::G16;
	}
	else
	{
		return ERawImageFormat::BGRA8;
	}
}

} // namespace
}