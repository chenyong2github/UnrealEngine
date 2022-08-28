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
	DefaultMaterialBakeSize = EGLTFExporterMaterialBakeSize::POT_1024;
	bExportVertexColors = false;
	bQuantizeVertexNormals = true;
	bQuantizeVertexTangents = true;
	DefaultLevelOfDetail = 0;
	bExportLevelSequences = true;
	bExportVertexSkinWeights = true;
	bExportAnimationSequences = true;
	bRetargetBoneTransforms = true;
	bExportPlaybackSettings = true;
	TextureCompression = EGLTFExporterTextureCompression::PNG;
	bExportTextureTransforms = true;
	bExportLightmaps = true;
	TextureHDREncoding = EGLTFExporterTextureHDREncoding::RGBM;
	ExportScale = 0.01;
	bExportHiddenInGame = false;
	ExportLights = EGLTFExporterLightMobility::MovableAndStationary;
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
