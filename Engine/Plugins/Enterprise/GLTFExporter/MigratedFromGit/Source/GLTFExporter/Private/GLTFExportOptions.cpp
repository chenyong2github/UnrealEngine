// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFExportOptions.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

UGLTFExportOptions::UGLTFExportOptions(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ResetToDefault();
}

void UGLTFExportOptions::ResetToDefault()
{
	bExportUnlitMaterials = true;
	bExportClearCoatMaterials = true;
	bExportExtraBlendModes = true;
	BakeMaterialInputs = EGLTFMaterialBakeMode::Simple;
	DefaultMaterialBakeSize = EGLTFMaterialBakeSizePOT::POT_1024;
	DefaultLevelOfDetail = 0;
	bExportVertexColors = false;
	bExportVertexSkinWeights = true;
	bExportMeshQuantization = true;
	bExportLevelSequences = true;
	bExportAnimationSequences = true;
	bRetargetBoneTransforms = true;
	bExportPlaybackSettings = true;
	TextureCompression = EGLTFTextureCompression::PNG;
	TextureCompressionQuality = 0;
	LosslessCompressTextures = static_cast<int32>(EGLTFTextureGroupFlags::All);
	bExportTextureTransforms = true;
	bExportLightmaps = true;
	TextureHDREncoding = EGLTFTextureHDREncoding::RGBM;
	ExportScale = 0.01;
	bExportHiddenInGame = false;
	ExportLights = EGLTFExportLightMobility::MovableAndStationary;
	bExportCameras = true;
	bExportCameraControls = true;
	bExportAnimationHotspots = true;
	bExportHDRIBackdrops = true;
	bExportSkySpheres = true;
	bExportVariantSets = true;
	ExportMaterialVariants = EGLTFMaterialBakeMode::Simple;
	bExportMeshVariants = true;
	bExportVisibilityVariants = true;
	bBundleWebViewer = true;
	bExportPreviewMesh = true;
	bAllExtensionsRequired = false;
	bShowFilesWhenDone = true;
}
