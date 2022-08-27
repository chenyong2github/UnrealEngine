// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"
#include "Json/GLTFJsonMesh.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"

class UVariant;
class UPropertyValueMaterial;

typedef TGLTFConverter<FGLTFJsonKhrMaterialVariant*, const UVariant*> IGLTFKhrMaterialVariantConverter;

class FGLTFKhrMaterialVariantConverter final : public FGLTFBuilderContext, public IGLTFKhrMaterialVariantConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonKhrMaterialVariant* Convert(const UVariant* Variant) override;

	bool TryParseMaterialProperty(FGLTFJsonPrimitive*& OutPrimitive, FGLTFJsonMaterial*& OutMaterialIndex, const UPropertyValueMaterial* Property) const;
};
