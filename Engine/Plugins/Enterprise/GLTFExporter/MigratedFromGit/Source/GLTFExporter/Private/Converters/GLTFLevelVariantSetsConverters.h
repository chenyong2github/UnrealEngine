// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Json/GLTFJsonLevelVariantSets.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"

class ULevelVariantSets;
class UVariantSet;
class UVariant;
class UVariantObjectBinding;
class UPropertyValue;

class FGLTFLevelVariantSetsConverter final : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonLevelVariantSetsIndex, const ULevelVariantSets*>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonLevelVariantSetsIndex Convert(const ULevelVariantSets* LevelVariantSets) override;

	bool TryParseVariant(FGLTFJsonVariant& OutVariant, const UVariant* Variant) const;
	bool TryParseVariantBinding(FGLTFJsonVariant& OutVariant, const UVariantObjectBinding* Binding) const;
	bool TryParseVisibilityPropertyValue(FGLTFJsonVariant& OutVariant, const UPropertyValue* Property) const;
	bool TryParseMaterialPropertyValue(FGLTFJsonVariant& OutVariant, const UPropertyValue* Property) const;
	bool TryParseStaticMeshPropertyValue(FGLTFJsonVariant& OutVariant, const UPropertyValue* Property) const;
	bool TryParseSkeletalMeshPropertyValue(FGLTFJsonVariant& OutVariant, const UPropertyValue* Property) const;
};
