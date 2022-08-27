// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"

typedef TGLTFConverter<FGLTFJsonLightMapIndex, const UStaticMeshComponent*> IGLTFLightMapConverter;

class FGLTFLightMapConverter final : public FGLTFBuilderContext, public IGLTFLightMapConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonLightMapIndex Convert(const UStaticMeshComponent* StaticMeshComponent) override;
};
