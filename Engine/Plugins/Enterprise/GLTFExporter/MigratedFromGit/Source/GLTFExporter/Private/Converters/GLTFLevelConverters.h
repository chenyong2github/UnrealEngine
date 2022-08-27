// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Engine.h"

class FGLTFSceneComponentConverter final : public TGLTFConverter<FGLTFJsonNodeIndex, const USceneComponent*>
{
	using TGLTFConverter::TGLTFConverter;

	FGLTFJsonNodeIndex Convert(const USceneComponent* SceneComponent) override;
};

class FGLTFActorConverter final : public TGLTFConverter<FGLTFJsonNodeIndex, const AActor*>
{
	using TGLTFConverter::TGLTFConverter;

	FGLTFJsonNodeIndex Convert(const AActor* Actor) override;
};

class FGLTFLevelConverter final : public TGLTFConverter<FGLTFJsonSceneIndex, const ULevel*>
{
	using TGLTFConverter::TGLTFConverter;

	FGLTFJsonSceneIndex Convert(const ULevel* Level) override;
};

class FGLTFCameraComponentConverter final : public TGLTFConverter<FGLTFJsonCameraIndex, const UCameraComponent*>
{
	using TGLTFConverter::TGLTFConverter;

	FGLTFJsonCameraIndex Convert(const UCameraComponent* CameraComponent) override;
};

class FGLTFLightComponentConverter final : public TGLTFConverter<FGLTFJsonLightIndex, const ULightComponent*>
{
	using TGLTFConverter::TGLTFConverter;

	FGLTFJsonLightIndex Convert(const ULightComponent* LightComponent) override;
};
