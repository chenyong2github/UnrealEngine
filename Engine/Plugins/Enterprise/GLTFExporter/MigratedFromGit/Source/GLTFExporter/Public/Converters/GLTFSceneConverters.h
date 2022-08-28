// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"

typedef TGLTFConverter<FGLTFJsonScene*, const UWorld*> IGLTFSceneConverter;

class GLTFEXPORTER_API FGLTFSceneConverter final : public FGLTFBuilderContext, public IGLTFSceneConverter
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonScene* Convert(const UWorld* Level) override;
};
