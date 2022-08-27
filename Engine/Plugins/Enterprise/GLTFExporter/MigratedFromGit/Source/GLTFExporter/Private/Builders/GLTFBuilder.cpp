// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builders/GLTFBuilder.h"
#include "Builders/GLTFBuilderUtility.h"

FGLTFBuilder::FGLTFBuilder(const FString& FilePath, const UGLTFExportOptions* ExportOptions)
	: bIsGlbFile(FGLTFBuilderUtility::IsGlbFile(FilePath))
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
		case EGLTFExporterTextureHDREncoding::None: return EGLTFJsonHDREncoding::None;
		case EGLTFExporterTextureHDREncoding::RGBM: return EGLTFJsonHDREncoding::RGBM;
		case EGLTFExporterTextureHDREncoding::RGBE: return EGLTFJsonHDREncoding::RGBE;
		default:
			checkNoEntry();
			return EGLTFJsonHDREncoding::None;
	}
}

bool FGLTFBuilder::ShouldExportLight(EComponentMobility::Type LightMobility) const
{
	switch (ExportOptions->ExportLights)
	{
		case EGLTFExporterLightMobility::All:
			return true;
		case EGLTFExporterLightMobility::MovableAndStationary:
			return LightMobility == EComponentMobility::Movable || LightMobility == EComponentMobility::Stationary;
		case EGLTFExporterLightMobility::MovableOnly:
			return LightMobility == EComponentMobility::Movable;
		default:
			return false;
	}
}
