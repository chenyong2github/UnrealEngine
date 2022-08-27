// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tasks/GLTFTask.h"
#include "Converters/GLTFMeshData.h"
#include "Builders/GLTFConvertBuilder.h"
#include "MaterialPropertyEx.h"
#include "Engine.h"

struct FGLTFPropertyBakeOutput;

class FGLTFMaterialTask : public FGLTFTask
{
public:

	FGLTFMaterialTask(FGLTFConvertBuilder& Builder, const UMaterialInterface* Material, const FGLTFMeshData* MeshData, TArray<int32> SectionIndices, FGLTFJsonMaterialIndex MaterialIndex)
		: FGLTFTask(EGLTFTaskPriority::Material)
		, Builder(Builder)
		, Material(Material)
		, MeshData(MeshData)
		, SectionIndices(SectionIndices)
		, MaterialIndex(MaterialIndex)
	{
	}

	virtual FString GetName() override
	{
		return Material->GetName();
	}

	virtual void Complete() override;

private:

	FGLTFConvertBuilder& Builder;
	const UMaterialInterface* Material;
	const FGLTFMeshData* MeshData;
	const TArray<int32> SectionIndices;
	const FGLTFJsonMaterialIndex MaterialIndex;

	bool TryGetAlphaMode(EGLTFJsonAlphaMode& OutAlphaMode, EGLTFJsonBlendMode& OutBlendMode) const;
	bool TryGetShadingModel(EGLTFJsonShadingModel& OutShadingModel) const;

	bool TryGetBaseColorAndOpacity(FGLTFJsonPBRMetallicRoughness& OutPBRParams, const FMaterialPropertyEx& BaseColorProperty, const FMaterialPropertyEx& OpacityProperty) const;
	bool TryGetMetallicAndRoughness(FGLTFJsonPBRMetallicRoughness& OutPBRParams, const FMaterialPropertyEx& MetallicProperty, const FMaterialPropertyEx& RoughnessProperty) const;
	bool TryGetClearCoatRoughness(FGLTFJsonClearCoatExtension& OutExtParams, const FMaterialPropertyEx& IntensityProperty, const FMaterialPropertyEx& RoughnessProperty) const;
	bool TryGetEmissive(FGLTFJsonMaterial& JsonMaterial, const FMaterialPropertyEx& EmissiveProperty) const;

	bool IsPropertyNonDefault(const FMaterialPropertyEx& Property) const;
	bool TryGetConstantColor(FGLTFJsonColor3& OutValue, const FMaterialPropertyEx& Property) const;
	bool TryGetConstantColor(FGLTFJsonColor4& OutValue, const FMaterialPropertyEx& Property) const;
	bool TryGetConstantColor(FLinearColor& OutValue, const FMaterialPropertyEx& Property) const;
	bool TryGetConstantScalar(float& OutValue, const FMaterialPropertyEx& Property) const;

	bool TryGetSourceTexture(FGLTFJsonTextureInfo& OutTexInfo, const FMaterialPropertyEx& Property, const TArray<FLinearColor>& AllowedMasks = {}) const;
	bool TryGetSourceTexture(const UTexture2D*& OutTexture, int32& OutTexCoord, FGLTFJsonTextureTransform& OutTransform, const FMaterialPropertyEx& Property, const TArray<FLinearColor>& AllowedMasks = {}) const;

	bool TryGetBakedMaterialProperty(FGLTFJsonTextureInfo& OutTexInfo, FGLTFJsonColor3& OutConstant, const FMaterialPropertyEx& Property, const FString& PropertyName) const;
	bool TryGetBakedMaterialProperty(FGLTFJsonTextureInfo& OutTexInfo, FGLTFJsonColor4& OutConstant, const FMaterialPropertyEx& Property, const FString& PropertyName) const;
	bool TryGetBakedMaterialProperty(FGLTFJsonTextureInfo& OutTexInfo, float& OutConstant, const FMaterialPropertyEx& Property, const FString& PropertyName) const;
	bool TryGetBakedMaterialProperty(FGLTFJsonTextureInfo& OutTexInfo, const FMaterialPropertyEx& Property, const FString& PropertyName) const;

	FGLTFPropertyBakeOutput BakeMaterialProperty(const FMaterialPropertyEx& Property, int32& OutTexCoord, bool bCopyAlphaFromRedChannel = false) const;
	FGLTFPropertyBakeOutput BakeMaterialProperty(const FMaterialPropertyEx& Property, int32& OutTexCoord, const FIntPoint& TextureSize, bool bCopyAlphaFromRedChannel = false) const;

	bool StoreBakedPropertyTexture(FGLTFJsonTextureInfo& OutTexInfo, const FGLTFPropertyBakeOutput& PropertyBakeOutput, const FString& PropertyName) const;

	FString GetMaterialName() const;
	FString GetBakedTextureName(const FString& PropertyName) const;
};
