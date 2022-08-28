// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Json/GLTFJsonLevelVariantSets.h"
#include "Converters/GLTFConverter.h"
#include "Engine.h"
#include "LevelVariantSetsActor.h"
#include "Variant.h"
#include "PropertyValue.h"
#include "LevelVariantSets.h"

class FGLTFLevelVariantSetsConverter final : public TGLTFConverter<FGLTFJsonLevelVariantSetsIndex, const ALevelVariantSetsActor*>
{
	using TGLTFConverter::TGLTFConverter;

	FGLTFJsonLevelVariantSetsIndex Convert(const ALevelVariantSetsActor* LevelVariantSetsActor) override;

	bool TryParseVariant(FGLTFJsonVariant& OutVariant, const UVariant* Variant) const;
	bool TryParseVariantBinding(FGLTFJsonVariant& OutVariant, const UVariantObjectBinding* Binding) const;
	bool TryParseVisibilityPropertyValue(FGLTFJsonVariant& OutVariant, const UPropertyValue* Property) const;
	bool TryParseMaterialPropertyValue(FGLTFJsonVariant& OutVariant, const UPropertyValue* Property) const;
	bool TryParseStaticMeshPropertyValue(FGLTFJsonVariant& OutVariant, const UPropertyValue* Property) const;
	bool TryParseSkeletalMeshPropertyValue(FGLTFJsonVariant& OutVariant, const UPropertyValue* Property) const;

	template<typename T>
	bool TryGetPropertyValue(UPropertyValue* Property, T& OutValue) const;

	FString GetLogContext(const UPropertyValue* Property) const;
	FString GetLogContext(const UVariantObjectBinding* Binding) const;
	FString GetLogContext(const UVariant* Variant) const;
	FString GetLogContext(const UVariantSet* VariantSet) const;
	FString GetLogContext(const ULevelVariantSets* LevelVariantSets) const;
};
