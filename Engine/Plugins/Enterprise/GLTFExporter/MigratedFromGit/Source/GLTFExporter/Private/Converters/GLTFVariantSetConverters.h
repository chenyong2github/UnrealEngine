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
	FGLTFJsonLevelVariantSetsIndex Add(FGLTFConvertBuilder& Builder, const FString& Name, const ALevelVariantSetsActor* LevelVariantSetsActor) override;

	bool TryParseVariant(FGLTFConvertBuilder& Builder, FGLTFJsonVariant& OutVariant, const UVariant* Variant) const;
	bool TryParseVariantBinding(FGLTFConvertBuilder& Builder, FGLTFJsonVariant& OutVariant, const UVariantObjectBinding* Binding) const;
	bool TryParseVisibilityPropertyValue(FGLTFConvertBuilder& Builder, FGLTFJsonVariant& OutVariant, const UPropertyValue* Property) const;
	bool TryParseMaterialPropertyValue(FGLTFConvertBuilder& Builder, FGLTFJsonVariant& OutVariant, const UPropertyValue* Property) const;

	// bool TryParseJsonVariantNode(FGLTFConvertBuilder& Builder, FGLTFJsonVariantNodeProperties& OutVariantNode, const UVariantObjectBinding* Binding) const;

	template<typename T>
	bool TryGetPropertyValue(UPropertyValue* Property, T& OutValue) const;

	FString GetLogContext(const UPropertyValue* Property) const;
	FString GetLogContext(const UVariantObjectBinding* Binding) const;
	FString GetLogContext(const UVariant* Variant) const;
	FString GetLogContext(const UVariantSet* VariantSet) const;
	FString GetLogContext(const ULevelVariantSets* LevelVariantSets) const;
};
