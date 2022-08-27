// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonCore.h"

struct GLTFEXPORTER_API FGLTFJsonSpotLight : IGLTFJsonObject
{
	float InnerConeAngle;
	float OuterConeAngle;

	FGLTFJsonSpotLight()
		: InnerConeAngle(0)
		, OuterConeAngle(HALF_PI)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};

struct GLTFEXPORTER_API FGLTFJsonLight : IGLTFJsonObject
{
	FString Name;

	EGLTFJsonLightType Type;

	FGLTFJsonColor3 Color;

	float Intensity;
	float Range;

	FGLTFJsonSpotLight Spot;

	FGLTFJsonLight()
		: Type(EGLTFJsonLightType::None)
		, Color(FGLTFJsonColor3::White)
		, Intensity(1)
		, Range(0)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};
