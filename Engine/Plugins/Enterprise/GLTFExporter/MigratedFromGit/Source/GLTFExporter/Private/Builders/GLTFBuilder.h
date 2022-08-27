// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GLTFExportOptions.h"
#include "Json/GLTFJsonEnums.h"
#include "UObject/GCObjectScopeGuard.h"

class FGLTFBuilder
{
protected:

	FGLTFBuilder(const FString& FilePath, const UGLTFExportOptions* ExportOptions);

public:

	const bool bIsGlbFile;
	const FString FilePath;
	const FString DirPath;

	// TODO: make ExportOptions private and expose each option via getters to ease overriding settings in future
	const UGLTFExportOptions* ExportOptions;

	const UMaterialInterface* ResolveProxy(const UMaterialInterface* Material) const;

	FIntPoint GetBakeSizeForMaterialProperty(const UMaterialInterface* Material, EGLTFMaterialPropertyGroup PropertyGroup) const;
	TextureFilter GetBakeFilterForMaterialProperty(const UMaterialInterface* Material, EGLTFMaterialPropertyGroup PropertyGroup) const;
	TextureAddress GetBakeTilingForMaterialProperty(const UMaterialInterface* Material, EGLTFMaterialPropertyGroup PropertyGroup) const;

	EGLTFJsonHDREncoding GetTextureHDREncoding() const;

	bool ShouldExportLight(EComponentMobility::Type LightMobility) const;

	int32 SanitizeLOD(const UStaticMesh* StaticMesh, const UStaticMeshComponent* StaticMeshComponent, int32 LODIndex) const;
	int32 SanitizeLOD(const USkeletalMesh* SkeletalMesh, const USkeletalMeshComponent* SkeletalMeshComponent, int32 LODIndex) const;

private:

	FGCObjectScopeGuard ExportOptionsGuard;

	static const UGLTFExportOptions* SanitizeExportOptions(const UGLTFExportOptions* Options);
	static EGLTFSceneMobility GetSceneMobility(EComponentMobility::Type Mobility);
};
