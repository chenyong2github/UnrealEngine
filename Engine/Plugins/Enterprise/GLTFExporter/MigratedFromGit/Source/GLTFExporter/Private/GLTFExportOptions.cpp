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
	bBakeMaterialInputs = true;
	bMaterialBakeUsingMeshData = false;
	DefaultMaterialBakeSize = EGLTFMaterialBakeSizePOT::POT_1024;
	bExportVertexColors = false;
	bExportMeshQuantization = true;
	DefaultLevelOfDetail = 0;
	bExportLevelSequences = true;
	bExportVertexSkinWeights = true;
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
	bExportHDRIBackdrops = true;
	bExportSkySpheres = true;
	bExportVariantSets = true;
	bExportAnimationHotspots = true;
	bBundleWebViewer = true;
	bExportPreviewMesh = true;
	bAllExtensionsRequired = false;
	bShowFilesWhenDone = true;
}
