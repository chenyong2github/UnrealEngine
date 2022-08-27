// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "GLTFExportOptions.h"
#include "Engine.h"

class FGLTFSceneComponentConverter final : public TGLTFConverter<FGLTFJsonNodeIndex, const USceneComponent*>
{
	using TGLTFConverter::TGLTFConverter;

	FGLTFJsonNodeIndex Convert(const FString& Name, const USceneComponent* SceneComponent) override;
};

class FGLTFActorConverter final : public TGLTFConverter<FGLTFJsonNodeIndex, const AActor*>
{
	using TGLTFConverter::TGLTFConverter;

	FGLTFJsonNodeIndex Convert(const FString& Name, const AActor* Actor) override;
};

class FGLTFLevelConverter final : public TGLTFConverter<FGLTFJsonSceneIndex, const ULevel*>
{
	using TGLTFConverter::TGLTFConverter;

	FGLTFJsonSceneIndex Convert(const FString& Name, const ULevel* Level) override;
};

class FGLTFCameraComponentConverter final : public TGLTFConverter<FGLTFJsonCameraIndex, const UCameraComponent*>
{
	using TGLTFConverter::TGLTFConverter;

	FGLTFJsonCameraIndex Convert(const FString& Name, const UCameraComponent* CameraComponent) override;
};

class FGLTFLightComponentConverter final : public TGLTFConverter<FGLTFJsonLightIndex, const ULightComponent*>
{
	using TGLTFConverter::TGLTFConverter;

	FGLTFJsonLightIndex Convert(const FString& Name, const ULightComponent* LightComponent) override;
};
