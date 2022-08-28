// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Core/GLTFColor.h"
#include "Json/GLTFJsonIndex.h"
#include "Converters/GLTFConverter.h"
#include "Converters/GLTFBuilderContext.h"
#include "Engine.h"

struct FGLTFJsonSkySphereColorCurve;

class FGLTFSkySphereConverter final : public FGLTFBuilderContext, public TGLTFConverter<FGLTFJsonSkySphereIndex, const AActor*>
{
	enum class ESkySphereTextureParameter
	{
		SkyTexture,
		CloudsTexture,
		StarsTexture
	};

	using FGLTFBuilderContext::FGLTFBuilderContext;

	virtual FGLTFJsonSkySphereIndex Convert(const AActor* SkySphereActor) override;

	template <class ValueType>
	void ConvertProperty(const AActor* Actor, const TCHAR* PropertyName, ValueType& OutValue) const;

	void ConvertColorProperty(const AActor* Actor, const TCHAR* PropertyName, FGLTFColor4& OutValue) const;
	void ConvertColorCurveProperty(const AActor* Actor, const TCHAR* PropertyName, FGLTFJsonSkySphereColorCurve& OutValue) const;
	void ConvertScalarParameter(const AActor* Actor, const UMaterialInstance* Material, const TCHAR* ParameterName, float& OutValue) const;

	void ConvertTextureParameter(const AActor* Actor, const UMaterialInstance* Material, const ESkySphereTextureParameter Parameter, FGLTFJsonTextureIndex& OutValue) const;
};
