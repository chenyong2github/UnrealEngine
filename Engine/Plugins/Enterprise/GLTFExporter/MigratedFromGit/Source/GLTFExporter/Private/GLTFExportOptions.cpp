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
	bBakeMaterialInputsUsingMeshData = true;
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
	bExportTextureTransforms = true;
	bExportLightmaps = true;
	TextureHDREncoding = EGLTFExporterTextureHDREncoding::RGBM;
	ExportScale = 0.01;
	bExportHiddenInGame = false;
	ExportLights = EGLTFExporterLightMobility::MovableAndStationary;
	bExportCameras = true;
	bExportOrbitalCameras = true;
	bExportHDRIBackdrops = true;
	bExportSkySpheres = true;
	bExportVariantSets = true;
	bExportAnimationHotspots = true;
	bBundleWebViewer = true;
	bExportPreviewMesh = true;
	bAllExtensionsRequired = false;
	bShowFilesWhenDone = true;
}

bool UGLTFExportOptions::CanEditChange(const FProperty* InProperty) const
{
	const FName PropertyFName = InProperty->GetFName();

	// TODO: remove options that have been implemented in exporters/converters.
	if (PropertyFName == GET_MEMBER_NAME_CHECKED(ThisClass, TextureHDREncoding) ||
		PropertyFName == GET_MEMBER_NAME_CHECKED(ThisClass, bExportHiddenInGame))
	{
		return false;
	}

	if (PropertyFName == GET_MEMBER_NAME_CHECKED(ThisClass, DefaultMaterialBakeSize))
	{
		return bBakeMaterialInputs;
	}

	if (PropertyFName == GET_MEMBER_NAME_CHECKED(ThisClass, bExportAnimationSequences))
	{
		return bExportVertexSkinWeights;
	}

	if (PropertyFName == GET_MEMBER_NAME_CHECKED(ThisClass, bRetargetBoneTransforms))
	{
		return bExportVertexSkinWeights && bExportAnimationSequences;
	}

	if (PropertyFName == GET_MEMBER_NAME_CHECKED(ThisClass, bExportOrbitalCameras))
	{
		return bExportCameras;
	}

	if (PropertyFName == GET_MEMBER_NAME_CHECKED(ThisClass, bBakeMaterialInputsUsingMeshData))
	{
		return bBakeMaterialInputs;
	}

	return true;
}

void UGLTFExportOptions::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyFName = PropertyChangedEvent.GetPropertyName();

	if (PropertyFName == GET_MEMBER_NAME_CHECKED(ThisClass, DefaultLevelOfDetail))
	{
		if (DefaultLevelOfDetail < 0)
		{
			DefaultLevelOfDetail = 0;
		}
	}
}
