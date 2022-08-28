// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFExportOptions.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"
#include "UObject/UObjectBaseUtility.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/App.h"

UGLTFExportOptions::UGLTFExportOptions(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bExportUnlitMaterials = true;
	bExportClearCoatMaterials = true;
	bBakeMaterialInputs = true;
	DefaultMaterialBakeSize = EGLTFExporterMaterialBakeSize::POT_1024;
	bExportVertexColors = true;
	bQuantizeVertexNormals = true;
	bQuantizeVertexTangents = true;
	DefaultLevelOfDetail = 0;
	bExportLevelSequences = true;
	bExportVertexSkinWeights = true;
	bExportAnimationSequences = true;
	bRetargetBoneTransforms = true;
	bExportPlaybackSettings = true;
	bExportTextureTransforms = true;
	bExportSourceTextures = true;
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

void UGLTFExportOptions::ResetToDefault()
{
	ReloadConfig();
}

void UGLTFExportOptions::LoadOptions()
{
	LoadConfig(nullptr, *GEditorPerProjectIni);
}

void UGLTFExportOptions::SaveOptions()
{
	SaveConfig(CPF_Config, *GEditorPerProjectIni);
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

FIntPoint UGLTFExportOptions::GetDefaultMaterialBakeSize() const
{
	const int32 Size = 1 << static_cast<int>(DefaultMaterialBakeSize);
	return { Size, Size };
}

bool UGLTFExportOptions::ShouldExportLight(EComponentMobility::Type LightMobility) const
{
	switch (ExportLights)
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
