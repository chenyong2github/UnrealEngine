// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"
#include "Engine.h"

class FGLTFSceneConverter final : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonSceneIndex, const ULevel*>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonSceneIndex Convert(const ULevel* Level) override;
};
