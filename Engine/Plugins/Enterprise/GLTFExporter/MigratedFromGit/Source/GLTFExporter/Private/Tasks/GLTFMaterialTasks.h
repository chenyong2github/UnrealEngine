// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tasks/GLTFTask.h"
#include "Converters/GLTFMeshData.h"
#include "Builders/GLTFConvertBuilder.h"
#include "Engine.h"

struct FGLTFPropertyBakeOutput;

class FGLTFMaterialTask : public FGLTFTask
{
public:

	FGLTFMaterialTask(FGLTFConvertBuilder& Builder, const UMaterialInterface* Material, const FGLTFMeshData* MeshData, FGLTFMaterialArray OverrideMaterials, FGLTFJsonMaterialIndex MaterialIndex)
        : FGLTFTask(EGLTFTaskPriority::Material)
		, Builder(Builder)
		, Material(Material)
		, MeshData(MeshData)
		, OverrideMaterials(OverrideMaterials)
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
	const FGLTFMaterialArray OverrideMaterials;
	const FGLTFJsonMaterialIndex MaterialIndex;

	bool TryGetAlphaMode(EGLTFJsonAlphaMode& AlphaMode) const;
	bool TryGetShadingModel(EGLTFJsonShadingModel& ShadingModel) const;

	bool TryGetBaseColorAndOpacity(FGLTFJsonPBRMetallicRoughness& OutPBRParams, EMaterialProperty BaseColorProperty, EMaterialProperty OpacityProperty) const;
	bool TryGetMetallicAndRoughness(FGLTFJsonPBRMetallicRoughness& OutPBRParams, EMaterialProperty MetallicProperty, EMaterialProperty RoughnessProperty) const;
	bool TryGetClearCoatRoughness(FGLTFJsonClearCoatExtension& OutExtParams, EMaterialProperty IntensityProperty, EMaterialProperty RoughnessProperty) const;
	bool TryGetEmissive(FGLTFJsonMaterial& JsonMaterial, EMaterialProperty EmissiveProperty) const;

	bool IsPropertyNonDefault(EMaterialProperty Property) const;
	bool TryGetConstantColor(FGLTFJsonColor3& OutValue, EMaterialProperty Property) const;
	bool TryGetConstantColor(FGLTFJsonColor4& OutValue, EMaterialProperty Property) const;
	bool TryGetConstantColor(FLinearColor& OutValue, EMaterialProperty Property) const;
	bool TryGetConstantScalar(float& OutValue, EMaterialProperty Property) const;

	bool TryGetSourceTexture(FGLTFJsonTextureInfo& OutTexInfo, EMaterialProperty Property, const TArray<FLinearColor>& AllowedMasks = {}) const;
	bool TryGetSourceTexture(const UTexture2D*& OutTexture, int32& OutTexCoord, FGLTFJsonTextureTransform& OutTransform, EMaterialProperty Property, const TArray<FLinearColor>& AllowedMasks = {}) const;

	bool TryGetBakedMaterialProperty(FGLTFJsonTextureInfo& OutTexInfo, FGLTFJsonColor3& OutConstant, EMaterialProperty Property, const FString& PropertyName) const;
	bool TryGetBakedMaterialProperty(FGLTFJsonTextureInfo& OutTexInfo, FGLTFJsonColor4& OutConstant, EMaterialProperty Property, const FString& PropertyName) const;
	bool TryGetBakedMaterialProperty(FGLTFJsonTextureInfo& OutTexInfo, float& OutConstant, EMaterialProperty Property, const FString& PropertyName) const;
	bool TryGetBakedMaterialProperty(FGLTFJsonTextureInfo& OutTexInfo, EMaterialProperty Property, const FString& PropertyName) const;

	FGLTFPropertyBakeOutput BakeMaterialProperty(EMaterialProperty Property, int32& OutTexCoord, const FIntPoint* PreferredTextureSize = nullptr, bool bCopyAlphaFromRedChannel = false) const;

	bool StoreBakedPropertyTexture(FGLTFJsonTextureInfo& OutTexInfo, const FGLTFPropertyBakeOutput& PropertyBakeOutput, const FString& PropertyName) const;

	FString GetMaterialName() const;
	FString GetBakedTextureName(const FString& PropertyName) const;
};
