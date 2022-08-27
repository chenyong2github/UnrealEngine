// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"
#include "Engine.h"

template <typename... InputTypes>
class TGLTFNodeConverter : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonNodeIndex, InputTypes...>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;
};

class FGLTFComponentConverter final : public TGLTFNodeConverter<const USceneComponent*>
{
	using TGLTFNodeConverter::TGLTFNodeConverter;

	virtual FGLTFJsonNodeIndex Convert(const USceneComponent* SceneComponent) override;
};

class FGLTFActorConverter final : public TGLTFNodeConverter<const AActor*>
{
	using TGLTFNodeConverter::TGLTFNodeConverter;

	virtual FGLTFJsonNodeIndex Convert(const AActor* Actor) override;
};
