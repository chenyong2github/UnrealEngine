// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Json/GLTFJsonLevelVariantSets.h"
#include "Converters/GLTFConverter.h"
#include "Engine.h"
#include "LevelVariantSetsActor.h"
#include "Variant.h"
#include "PropertyValue.h"

class FGLTFLevelVariantSetsConverter final : public TGLTFConverter<FGLTFJsonLevelVariantSetsIndex, const ALevelVariantSetsActor*>
{
	FGLTFJsonLevelVariantSetsIndex Add(FGLTFConvertBuilder& Builder, const FString& Name, const ALevelVariantSetsActor* LevelVariantSetsActor) override;

	bool TryParseJsonVariant(FGLTFConvertBuilder& Builder, FGLTFJsonVariant& OutVariant, const UVariant* Variant, const FString& ThumbnailPrefix = TEXT("")) const;
	bool TryParseJsonVariantNode(FGLTFConvertBuilder& Builder, FGLTFJsonVariantNode& OutVariantNode, const UVariantObjectBinding* Binding) const;

	template<typename T>
	bool TryGetPropertyValue(UPropertyValue* Property, T& OutValue) const;
};
