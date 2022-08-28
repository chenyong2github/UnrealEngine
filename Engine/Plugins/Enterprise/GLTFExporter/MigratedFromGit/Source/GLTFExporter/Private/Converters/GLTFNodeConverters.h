// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"
#include "Engine.h"

class FGLTFSceneComponentConverter : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonNodeIndex, const USceneComponent*>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	FGLTFJsonNodeIndex Convert(const USceneComponent* SceneComponent) override final;
};

class FGLTFActorConverter : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonNodeIndex, const AActor*>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	FGLTFJsonNodeIndex Convert(const AActor* Actor) override final;
};
