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

class FGLTFLevelConverter : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonSceneIndex, const ULevel*>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	FGLTFJsonSceneIndex Convert(const ULevel* Level) override final;
};

class FGLTFCameraComponentConverter : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonCameraIndex, const UCameraComponent*>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	FGLTFJsonCameraIndex Convert(const UCameraComponent* CameraComponent) override final;
};

class FGLTFLightComponentConverter : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonLightIndex, const ULightComponent*>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	FGLTFJsonLightIndex Convert(const ULightComponent* LightComponent) override final;
};
