// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builders/GLTFBuilder.h"
#include "UserData/GLTFMaterialUserData.h"
#include "Builders/GLTFFileUtility.h"

FGLTFBuilder::FGLTFBuilder(const FString& FilePath, const UGLTFExportOptions* ExportOptions)
	: bIsGlbFile(FGLTFFileUtility::IsGlbFile(FilePath))
	, FilePath(FilePath)
	, DirPath(FPaths::GetPath(FilePath))
	, ExportOptions(ValidateExportOptions(ExportOptions))
	, ExportOptionsGuard(ExportOptions)
{
}

FIntPoint FGLTFBuilder::GetBakeSizeForMaterialProperty(const UMaterialInterface* Material, EGLTFMaterialPropertyGroup PropertyGroup) const
{
	EGLTFMaterialBakeSizePOT DefaultValue = ExportOptions->DefaultMaterialBakeSize;
	if (const FGLTFOverrideMaterialBakeSettings* BakeSettings = ExportOptions->DefaultInputBakeSettings.Find(PropertyGroup))
	{
		if (BakeSettings->bOverrideSize)
		{
			DefaultValue = BakeSettings->Size;
		}
	}

	const EGLTFMaterialBakeSizePOT Size = UGLTFMaterialExportOptions::GetBakeSizeForPropertyGroup(Material, PropertyGroup, DefaultValue);
	const int32 PixelSize = 1 << static_cast<uint8>(Size);
	return { PixelSize, PixelSize };
}

TextureFilter FGLTFBuilder::GetBakeFilterForMaterialProperty(const UMaterialInterface* Material, EGLTFMaterialPropertyGroup PropertyGroup) const
{
	TextureFilter DefaultValue = ExportOptions->DefaultMaterialBakeFilter;
	if (const FGLTFOverrideMaterialBakeSettings* BakeSettings = ExportOptions->DefaultInputBakeSettings.Find(PropertyGroup))
	{
		if (BakeSettings->bOverrideFilter)
		{
			DefaultValue = BakeSettings->Filter;
		}
	}

	return UGLTFMaterialExportOptions::GetBakeFilterForPropertyGroup(Material, PropertyGroup, DefaultValue);
}

TextureAddress FGLTFBuilder::GetBakeTilingForMaterialProperty(const UMaterialInterface* Material, EGLTFMaterialPropertyGroup PropertyGroup) const
{
	TextureAddress DefaultValue = ExportOptions->DefaultMaterialBakeTiling;
	if (const FGLTFOverrideMaterialBakeSettings* BakeSettings = ExportOptions->DefaultInputBakeSettings.Find(PropertyGroup))
	{
		if (BakeSettings->bOverrideTiling)
		{
			DefaultValue = BakeSettings->Tiling;
		}
	}

	return UGLTFMaterialExportOptions::GetBakeTilingForPropertyGroup(Material, PropertyGroup, DefaultValue);
}

EGLTFJsonHDREncoding FGLTFBuilder::GetTextureHDREncoding() const
{
	switch (ExportOptions->TextureHDREncoding)
	{
		case EGLTFTextureHDREncoding::None: return EGLTFJsonHDREncoding::None;
		case EGLTFTextureHDREncoding::RGBM: return EGLTFJsonHDREncoding::RGBM;
		// TODO: add more encodings (like RGBE) when viewer supports them
		default:
			checkNoEntry();
			return EGLTFJsonHDREncoding::None;
	}
}

bool FGLTFBuilder::ShouldExportLight(EComponentMobility::Type LightMobility) const
{
	const EGLTFSceneMobility AllowedMobility = static_cast<EGLTFSceneMobility>(ExportOptions->ExportLights);
	const EGLTFSceneMobility QueriedMobility = GetSceneMobility(LightMobility);
	return EnumHasAllFlags(AllowedMobility, QueriedMobility);
}

const UGLTFExportOptions* FGLTFBuilder::ValidateExportOptions(const UGLTFExportOptions* ExportOptions)
{
	if (!FApp::CanEverRender())
	{
		if (ExportOptions->BakeMaterialInputs != EGLTFMaterialBakeMode::Disabled || ExportOptions->TextureImageFormat != EGLTFTextureImageFormat::None)
		{
			// TODO: warn the following options requires rendering support and will be overriden
			UGLTFExportOptions* OverridenOptions = DuplicateObject(ExportOptions, nullptr);
			OverridenOptions->BakeMaterialInputs = EGLTFMaterialBakeMode::Disabled;
			OverridenOptions->TextureImageFormat = EGLTFTextureImageFormat::None;
			return OverridenOptions;
		}
	}

	return ExportOptions;
}

EGLTFSceneMobility FGLTFBuilder::GetSceneMobility(EComponentMobility::Type Mobility)
{
	switch (Mobility)
	{
		case EComponentMobility::Static:     return EGLTFSceneMobility::Static;
		case EComponentMobility::Stationary: return EGLTFSceneMobility::Stationary;
		case EComponentMobility::Movable:    return EGLTFSceneMobility::Movable;
		default:
			checkNoEntry();
			return EGLTFSceneMobility::None;
	}
}
