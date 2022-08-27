// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Json/GLTFJsonMaterial.h"
#include "Converters/GLTFConverter.h"
#include "Engine.h"

struct FGLTFPropertyBakeOutput;

class FGLTFMaterialConverter final : public TGLTFConverter<FGLTFJsonMaterialIndex, const UMaterialInterface*>
{
	FGLTFJsonMaterialIndex Add(FGLTFConvertBuilder& Builder, const FString& Name, const UMaterialInterface* Material) override;

	bool TryGetShadingModel(FGLTFConvertBuilder& Builder, EGLTFJsonShadingModel& ShadingModel, const UMaterialInterface* Material) const;

	bool TryGetBaseColorAndOpacity(FGLTFConvertBuilder& Builder, FGLTFJsonPBRMetallicRoughness& OutPBRParams, const UMaterialInterface* Material, EMaterialProperty BaseColorProperty, EMaterialProperty OpacityProperty) const;
	bool TryGetMetallicAndRoughness(FGLTFConvertBuilder& Builder, FGLTFJsonPBRMetallicRoughness& OutPBRParams, const UMaterialInterface* Material, EMaterialProperty MetallicProperty, EMaterialProperty RoughnessProperty) const;
	bool TryGetClearCoatRoughness(FGLTFConvertBuilder& Builder, FGLTFJsonClearCoatExtension& OutExtParams, const UMaterialInterface* Material, EMaterialProperty IntensityProperty, EMaterialProperty RoughnessProperty) const;
	bool TryGetEmissive(FGLTFConvertBuilder& Builder, FGLTFJsonMaterial& JsonMaterial, EMaterialProperty EmissiveProperty, const UMaterialInterface* Material) const;

	bool IsPropertyNonDefault(EMaterialProperty Property, const UMaterialInterface* Material) const;
	bool TryGetConstantColor(FGLTFJsonColor3& OutValue, EMaterialProperty Property, const UMaterialInterface* Material) const;
	bool TryGetConstantColor(FGLTFJsonColor4& OutValue, EMaterialProperty Property, const UMaterialInterface* Material) const;
	bool TryGetConstantColor(FLinearColor& OutValue, EMaterialProperty Property, const UMaterialInterface* Material) const;
	bool TryGetConstantScalar(float& OutValue, EMaterialProperty Property, const UMaterialInterface* Material) const;

	bool TryGetSourceTexture(FGLTFConvertBuilder& Builder, FGLTFJsonTextureInfo& OutTexInfo, EMaterialProperty Property, const UMaterialInterface* Material, const TArray<FLinearColor>& AllowedMasks = {}) const;
	bool TryGetSourceTexture(const UTexture2D*& OutTexture, int32& OutTexCoord, EMaterialProperty Property, const UMaterialInterface* Material, const TArray<FLinearColor>& AllowedMasks = {}) const;

	bool TryGetBakedMaterialProperty(FGLTFConvertBuilder& Builder, FGLTFJsonTextureInfo& OutTexInfo, FGLTFJsonColor3& OutConstant, EMaterialProperty Property, const FString& PropertyName, const UMaterialInterface* Material) const;
	bool TryGetBakedMaterialProperty(FGLTFConvertBuilder& Builder, FGLTFJsonTextureInfo& OutTexInfo, FGLTFJsonColor4& OutConstant, EMaterialProperty Property, const FString& PropertyName, const UMaterialInterface* Material) const;
	bool TryGetBakedMaterialProperty(FGLTFConvertBuilder& Builder, FGLTFJsonTextureInfo& OutTexInfo, float& OutConstant, EMaterialProperty Property, const FString& PropertyName, const UMaterialInterface* Material) const;
	bool TryGetBakedMaterialProperty(FGLTFConvertBuilder& Builder, FGLTFJsonTextureInfo& OutTexInfo, EMaterialProperty Property, const FString& PropertyName, const UMaterialInterface* Material) const;

	FGLTFPropertyBakeOutput BakeMaterialProperty(FGLTFConvertBuilder& Builder, EMaterialProperty Property, const UMaterialInterface* Material, int32& OutTexCoord, const FIntPoint* PreferredTextureSize = nullptr, bool bCopyAlphaFromRedChannel = false) const;

	bool StoreBakedPropertyTexture(FGLTFConvertBuilder& Builder, FGLTFJsonTextureInfo& OutTexInfo, const FGLTFPropertyBakeOutput& PropertyBakeOutput, const FString& PropertyName, const UMaterialInterface* Material) const;
};
