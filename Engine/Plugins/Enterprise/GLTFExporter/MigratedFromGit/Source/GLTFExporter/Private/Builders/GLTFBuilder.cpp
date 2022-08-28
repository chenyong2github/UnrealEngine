// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builders/GLTFBuilder.h"
#include "Builders/GLTFFileUtility.h"

FGLTFBuilder::FGLTFBuilder(const FString& FilePath, const UGLTFExportOptions* ExportOptions)
	: bIsGlbFile(FGLTFFileUtility::IsGlbFile(FilePath))
	, FilePath(FilePath)
	, DirPath(FPaths::GetPath(FilePath))
	, ExportOptions(ExportOptions)
{
}

FIntPoint FGLTFBuilder::GetDefaultMaterialBakeSize() const
{
	const int32 Size = 1 << static_cast<int>(ExportOptions->DefaultMaterialBakeSize);
	return { Size, Size };
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
	switch (ExportOptions->ExportLights)
	{
		case EGLTFExportLightMobility::All:
			return true;
		case EGLTFExportLightMobility::MovableAndStationary:
			return LightMobility == EComponentMobility::Movable || LightMobility == EComponentMobility::Stationary;
		case EGLTFExportLightMobility::MovableOnly:
			return LightMobility == EComponentMobility::Movable;
		default:
			return false;
	}
}
