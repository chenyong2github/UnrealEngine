// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Engine.h"

class FGLTFLightMapConverter final : public TGLTFConverter<FGLTFJsonLightMapIndex, const UStaticMeshComponent*>
{
	using TGLTFConverter::TGLTFConverter;

	FGLTFJsonLightMapIndex Convert(const UStaticMeshComponent* StaticMeshComponent) override;
};
