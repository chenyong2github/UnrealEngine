// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"
#include "Engine.h"

template <typename... InputTypes>
class FGLTFNodeConverter : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonNodeIndex, InputTypes...>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;
};

class FGLTFComponentConverter final : public FGLTFNodeConverter<const USceneComponent*>
{
	using FGLTFNodeConverter::FGLTFNodeConverter;

	virtual FGLTFJsonNodeIndex Convert(const USceneComponent* SceneComponent) override;
};

class FGLTFActorConverter final : public FGLTFNodeConverter<const AActor*>
{
	using FGLTFNodeConverter::FGLTFNodeConverter;

	virtual FGLTFJsonNodeIndex Convert(const AActor* Actor) override;
};
