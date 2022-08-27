// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Json/GLTFJsonMaterial.h"
#include "Converters/GLTFConverter.h"
#include "Engine.h"

class FGLTFMaterialConverter final : public TGLTFConverter<FGLTFJsonMaterialIndex, const UMaterialInterface*>
{
	FGLTFJsonMaterialIndex Add(FGLTFConvertBuilder& Builder, const FString& Name, const UMaterialInterface* Material) override;

	bool TryGetSimpleBaseColorOpacity(FGLTFConvertBuilder& Builder, FGLTFJsonMaterial& OutValue, const FColorMaterialInput& BaseColorInput, const FScalarMaterialInput& OpacityInput, const UMaterialInstance* MaterialInstance) const;
	bool TryGetSimpleMetallicRoughness(FGLTFConvertBuilder& Builder, FGLTFJsonPBRMetallicRoughness& OutValue, const FScalarMaterialInput& MetallicInput, const FScalarMaterialInput& RoughnessInput, const UMaterialInstance* MaterialInstance) const;

	bool TryGetConstantColor(FGLTFJsonColor3& OutValue, const FColorMaterialInput& MaterialInput, const UMaterialInstance* MaterialInstance) const;
	bool TryGetConstantColor(FGLTFJsonColor4& OutValue, const FColorMaterialInput& MaterialInput, const UMaterialInstance* MaterialInstance) const;
	bool TryGetConstantColor(FLinearColor& OutValue, const FColorMaterialInput& MaterialInput, const UMaterialInstance* MaterialInstance) const;
	bool TryGetConstantScalar(float& OutValue, const FScalarMaterialInput& MaterialInput, const UMaterialInstance* MaterialInstance) const;

	bool TryGetSourceTexture(FGLTFConvertBuilder& Builder, FGLTFJsonTextureInfo& OutValue, const FExpressionInput& MaterialInput, const UMaterialInstance* MaterialInstance) const;
	bool TryGetSourceTexture(const UTexture2D*& OutTexture, int32& OutTexCoord, int32& OutMask, const FExpressionInput& MaterialInput, const UMaterialInstance* MaterialInstance) const;
};
