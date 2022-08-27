// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Engine.h"

class FGLTFLightMapConverter final : public TGLTFConverter<FGLTFJsonLightMapIndex, const UStaticMeshComponent*>
{
	FGLTFJsonLightMapIndex Add(FGLTFConvertBuilder& Builder, const FString& Name, const UStaticMeshComponent* StaticMeshComponent) override;
};
