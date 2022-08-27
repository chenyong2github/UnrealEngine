// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"
#include "Engine.h"

struct FGLTFJsonColor4;

class FGLTFSkySphereConverter final : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonSkySphereIndex, const AActor*>
{
	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonSkySphereIndex Convert(const AActor* SkySphereActor) override;

	const UTexture2D* GetSkyTexture(const UMaterialInstance* SkyMaterial) const;
	const UTexture2D* GetCloudsTexture(const UMaterialInstance* SkyMaterial) const;
	const UTexture2D* GetStarsTexture(const UMaterialInstance* SkyMaterial) const;

	template <class ValueType>
	void ConvertProperty(const AActor& Actor, const TCHAR* PropertyName, const TCHAR* ExportedPropertyName, ValueType& OutValue) const;

	void ConvertColorProperty(const AActor& Actor, const TCHAR* PropertyName, const TCHAR* ExportedPropertyName, FGLTFJsonColor4& OutValue) const;
	void ConvertScalarParameter(const AActor& Actor, const UMaterialInstance* Material, const TCHAR* ParameterName, const TCHAR* ExportedPropertyName, float& OutValue) const;
};
