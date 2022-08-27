// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Engine.h"

class FGLTFSceneComponentConverter final : public TGLTFConverter<FGLTFJsonNodeIndex, const USceneComponent*>
{
	FGLTFJsonNodeIndex Add(FGLTFConvertBuilder& Builder, const FString& Name, const USceneComponent* SceneComponent) override;
};

class FGLTFActorConverter final : public TGLTFConverter<FGLTFJsonNodeIndex, const AActor*>
{
	FGLTFJsonNodeIndex Add(FGLTFConvertBuilder& Builder, const FString& Name, const AActor* Actor) override;
};

class FGLTFLevelConverter final : public TGLTFConverter<FGLTFJsonSceneIndex, const ULevel*>
{
	FGLTFJsonSceneIndex Add(FGLTFConvertBuilder& Builder, const FString& Name, const ULevel* Level) override;
};

class FGLTFCameraComponentConverter final : public TGLTFConverter<FGLTFJsonCameraIndex, const UCameraComponent*>
{
	FGLTFJsonCameraIndex Add(FGLTFConvertBuilder& Builder, const FString& Name, const UCameraComponent* CameraComponent) override;
};
