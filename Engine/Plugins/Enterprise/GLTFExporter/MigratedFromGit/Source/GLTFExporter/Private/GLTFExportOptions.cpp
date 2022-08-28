// Copyriyright Epic Games, Inc. All Rights Reserved.

#include "GLTFExportOptions.h"

UGLTFExportOptions::UGLTFExportOptions(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bEmbedVertexData = true;
	bExportVertexColor = true;
	bExportPreviewMesh = true;
	bExportUnlitMaterial = true;
	bExportClearCoatMaterial = true;
	bBakeMaterialInputs = true;
	BakedMaterialInputSize = EGLTFExporterTextureSize::POT_512;
	bEmbedTextures = true;
	TextureFormat = EGLTFExporterTextureFormat::PNG;
	bExportLightmaps = true;
	bExportAnyActor = true;
	bExportLight = true;
	bExportCamera = true;
	bExportReflectionCapture = true;
	bExportHDRIBackdrop = true;
	bExportVariantSets = true;
	bExportAnimationTrigger = true;
}

void UGLTFExportOptions::ResetToDefault()
{
	ReloadConfig();
}

void UGLTFExportOptions::LoadOptions()
{
}

void UGLTFExportOptions::SaveOptions()
{
}
