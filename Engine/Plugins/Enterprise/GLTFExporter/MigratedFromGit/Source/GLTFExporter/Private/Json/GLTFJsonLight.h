// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonObject.h"
#include "Json/GLTFJsonEnums.h"
#include "Json/GLTFJsonColor.h"

struct FGLTFJsonSpotLight : IGLTFJsonObject
{
	float InnerConeAngle;
	float OuterConeAngle;

	FGLTFJsonSpotLight()
		: InnerConeAngle(0)
		, OuterConeAngle(PI / 2.0f)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		if (InnerConeAngle != 0)
		{
			Writer.Write(TEXT("innerConeAngle"), InnerConeAngle);
		}

		if (OuterConeAngle != PI / 2.0f)
		{
			Writer.Write(TEXT("outerConeAngle"), OuterConeAngle);
		}
	}
};

struct FGLTFJsonLight : IGLTFJsonObject
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

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		if (!Name.IsEmpty())
		{
			Writer.Write(TEXT("name"), Name);
		}

		Writer.Write(TEXT("type"), Type);

		if (!Color.IsNearlyEqual(FGLTFJsonColor3::White))
		{
			Writer.Write(TEXT("color"), Color);
		}

		if (Intensity != 1)
		{
			Writer.Write(TEXT("intensity"), Intensity);
		}

		if (Type == EGLTFJsonLightType::Point || Type == EGLTFJsonLightType::Spot)
		{
			if (Range != 0)
			{
				Writer.Write(TEXT("range"), Range);
			}

			if (Type == EGLTFJsonLightType::Spot)
			{
				Writer.Write(TEXT("spot"), Spot);
			}
		}
	}
};
