// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"
#include "Json/GLTFJsonMesh.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"

class UVariant;
class UPropertyValueMaterial;

typedef TGLTFConverter<FGLTFJsonKhrMaterialVariantIndex, const UVariant*> IGLTFKhrMaterialVariantConverter;

class FGLTFKhrMaterialVariantConverter final : public FGLTFBuilderContext, public IGLTFKhrMaterialVariantConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonKhrMaterialVariantIndex Convert(const UVariant* Variant) override;

	bool TryParseMaterialProperty(FGLTFJsonPrimitive*& OutPrimitive, FGLTFJsonMaterialIndex& OutMaterialIndex, const UPropertyValueMaterial* Property) const;
};
