// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Engine.h"

class GLTFEXPORTER_API FGLTFSceneComponentConverter final : public TGLTFConverter<FGLTFJsonNodeIndex, TTuple<const USceneComponent*, bool, bool>>
{
	FGLTFJsonNodeIndex Add(FGLTFConvertBuilder& Builder, const FString& Name, TTuple<const USceneComponent*, bool, bool> Params) override;
};

class GLTFEXPORTER_API FGLTFLevelConverter final : public TGLTFConverter<FGLTFJsonSceneIndex, TTuple<const ULevel*, bool>>
{
	FGLTFJsonSceneIndex Add(FGLTFConvertBuilder& Builder, const FString& Name, TTuple<const ULevel*, bool> Params) override;
};
