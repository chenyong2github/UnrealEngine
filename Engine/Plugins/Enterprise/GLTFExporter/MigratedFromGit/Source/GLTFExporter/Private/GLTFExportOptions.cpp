// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFExportOptions.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"

UGLTFExportOptions::UGLTFExportOptions(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ResetToDefault();
}

void UGLTFExportOptions::ResetToDefault()
{
	ExportUniformScale = 0.01;
	bExportPreviewMesh = true;
	bStrictCompliance = true;
	bSkipNearDefaultValues = true;
	bIncludeGeneratorVersion = true;
	bExportUnlitMaterials = true;
	bExportClearCoatMaterials = true;
	bExportExtraBlendModes = false;
	BakeMaterialInputs = EGLTFMaterialBakeMode::UseMeshData;
	DefaultMaterialBakeSize = EGLTFMaterialBakeSizePOT::POT_1024;
	DefaultMaterialBakeFilter = TF_Trilinear;
	DefaultMaterialBakeTiling = TA_Wrap;
	bExportVertexColors = false;
	bExportVertexSkinWeights = true;
	bUseMeshQuantization = false;
	bExportLevelSequences = true;
	bExportAnimationSequences = true;
	bRetargetBoneTransforms = true;
	bExportPlaybackSettings = false;
	TextureImageFormat = EGLTFTextureImageFormat::PNG;
	TextureImageQuality = 0;
	NoLossyImageFormatFor = static_cast<int32>(EGLTFTextureType::All);
	bExportTextureTransforms = true;
	bExportLightmaps = false;
	TextureHDREncoding = EGLTFTextureHDREncoding::None;
	bExportHiddenInGame = false;
	ExportLights = static_cast<int32>(EGLTFSceneMobility::Stationary | EGLTFSceneMobility::Movable);
	bExportCameras = true;
	bExportCameraControls = false;
	bExportAnimationHotspots = false;
	bExportHDRIBackdrops = false;
	bExportSkySpheres = false;
	bExportVariantSets = false;
	ExportMaterialVariants = EGLTFMaterialVariantMode::UseMeshData;
	bExportMeshVariants = true;
	bExportVisibilityVariants = true;
}
