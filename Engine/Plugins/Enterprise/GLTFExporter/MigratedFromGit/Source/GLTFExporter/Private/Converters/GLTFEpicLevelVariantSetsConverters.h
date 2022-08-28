// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Json/GLTFJsonEpicLevelVariantSets.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"

class ULevelVariantSets;
class UVariantSet;
class UVariant;
class UVariantObjectBinding;
class UPropertyValue;

class FGLTFEpicLevelVariantSetsConverter final : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonEpicLevelVariantSetsIndex, const ULevelVariantSets*>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonEpicLevelVariantSetsIndex Convert(const ULevelVariantSets* LevelVariantSets) override;

	bool TryParseVariant(FGLTFJsonEpicVariant& OutVariant, const UVariant* Variant) const;
	bool TryParseVariantBinding(FGLTFJsonEpicVariant& OutVariant, const UVariantObjectBinding* Binding) const;
	bool TryParseVisibilityPropertyValue(FGLTFJsonEpicVariant& OutVariant, const UPropertyValue* Property) const;
	bool TryParseMaterialPropertyValue(FGLTFJsonEpicVariant& OutVariant, const UPropertyValue* Property) const;
	bool TryParseStaticMeshPropertyValue(FGLTFJsonEpicVariant& OutVariant, const UPropertyValue* Property) const;
	bool TryParseSkeletalMeshPropertyValue(FGLTFJsonEpicVariant& OutVariant, const UPropertyValue* Property) const;
};
