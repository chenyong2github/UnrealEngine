// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"
#include "Engine.h"

class FGLTFSkySphereConverter final : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonSkySphereIndex, const AActor*>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonSkySphereIndex Convert(const AActor* SkySphereActor) override;

	const UTexture2D* GetSkyTexture(const UMaterialInstance* SkyMaterial) const;
	const UTexture2D* GetCloudsTexture(const UMaterialInstance* SkyMaterial) const;
	const UTexture2D* GetStarsTexture(const UMaterialInstance* SkyMaterial) const;
};
