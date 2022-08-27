// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"
#include "Engine.h"

class FGLTFMaterialConverter final : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonMaterialIndex, const UMaterialInterface*>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonMaterialIndex Convert(const UMaterialInterface* Material) override;
};
