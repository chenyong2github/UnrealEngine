// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonObject.h"
#include "Json/GLTFJsonIndex.h"
#include "Json/GLTFJsonColor.h"

struct FGLTFJsonSkySphereColorCurve : IGLTFJsonArray
{
	struct FKey
	{
		float Time;
		float Value;
	};

	struct FComponentCurve
	{
		TArray<FKey> Keys;
	};

	TArray<FComponentCurve> ComponentCurves;

	virtual void WriteArray(IGLTFJsonWriter& Writer) const override
	{
		for (int32 ComponentIndex = 0; ComponentIndex < ComponentCurves.Num(); ++ComponentIndex)
		{
			const FComponentCurve& ComponentCurve = ComponentCurves[ComponentIndex];

			Writer.StartArray();

			for (const FKey& Key: ComponentCurve.Keys)
			{
				Writer.Write(Key.Time);
				Writer.Write(Key.Value);
			}

			Writer.EndArray();
		}
	}
};

struct FGLTFJsonSkySphere : IGLTFJsonObject
{
	FString Name;

	FGLTFJsonMeshIndex    SkySphereMesh;
	FGLTFJsonTextureIndex SkyTexture;
	FGLTFJsonTextureIndex CloudsTexture;
	FGLTFJsonTextureIndex StarsTexture;
	FGLTFJsonNodeIndex    DirectionalLight;

	float SunHeight;
	float SunBrightness;
	float StarsBrightness;
	float CloudSpeed;
	float CloudOpacity;
	float HorizonFalloff;

	float SunRadius;
	float NoisePower1;
	float NoisePower2;

	bool bColorsDeterminedBySunPosition;

	FGLTFJsonColor4 ZenithColor;
	FGLTFJsonColor4 HorizonColor;
	FGLTFJsonColor4 CloudColor;
	FGLTFJsonColor4 OverallColor;

	FGLTFJsonSkySphereColorCurve ZenithColorCurve;
	FGLTFJsonSkySphereColorCurve HorizonColorCurve;
	FGLTFJsonSkySphereColorCurve CloudColorCurve;

	FGLTFJsonVector3 Scale;

	FGLTFJsonSkySphere()
		: SunHeight(0)
		, SunBrightness(0)
		, StarsBrightness(0)
		, CloudSpeed(0)
		, CloudOpacity(0)
		, HorizonFalloff(0)
		, SunRadius(0)
		, NoisePower1(0)
		, NoisePower2(0)
		, bColorsDeterminedBySunPosition(false)
		, ZenithColor(FGLTFJsonColor4::White)
		, HorizonColor(FGLTFJsonColor4::White)
		, CloudColor(FGLTFJsonColor4::White)
		, OverallColor(FGLTFJsonColor4::White)
		, Scale(FGLTFJsonVector3::One)
	{
	}

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override
	{
		if (!Name.IsEmpty())
		{
			Writer.Write(TEXT("name"), Name);
		}

		Writer.Write(TEXT("skySphereMesh"), SkySphereMesh);
		Writer.Write(TEXT("skyTexture"), SkyTexture);
		Writer.Write(TEXT("cloudsTexture"), CloudsTexture);
		Writer.Write(TEXT("starsTexture"), StarsTexture);

		if (DirectionalLight != INDEX_NONE)
		{
			Writer.Write(TEXT("directionalLight"), DirectionalLight);
		}

		Writer.Write(TEXT("sunHeight"), SunHeight);
		Writer.Write(TEXT("sunBrightness"), SunBrightness);
		Writer.Write(TEXT("starsBrightness"), StarsBrightness);
		Writer.Write(TEXT("cloudSpeed"), CloudSpeed);
		Writer.Write(TEXT("cloudOpacity"), CloudOpacity);
		Writer.Write(TEXT("horizonFalloff"), HorizonFalloff);

		Writer.Write(TEXT("sunRadius"), SunRadius);
		Writer.Write(TEXT("noisePower1"), NoisePower1);
		Writer.Write(TEXT("noisePower2"), NoisePower2);

		Writer.Write(TEXT("colorsDeterminedBySunPosition"), bColorsDeterminedBySunPosition);

		Writer.Write(TEXT("zenithColor"), ZenithColor);
		Writer.Write(TEXT("horizonColor"), HorizonColor);
		Writer.Write(TEXT("cloudColor"), CloudColor);

		if (!OverallColor.IsNearlyEqual(FGLTFJsonColor4::White, Writer.DefaultTolerance))
		{
			Writer.Write(TEXT("overallColor"), OverallColor);
		}

		if (ZenithColorCurve.ComponentCurves.Num() >= 3)
		{
			Writer.Write(TEXT("zenithColorCurve"), ZenithColorCurve);
		}

		if (HorizonColorCurve.ComponentCurves.Num() >= 3)
		{
			Writer.Write(TEXT("horizonColorCurve"), HorizonColorCurve);
		}

		if (CloudColorCurve.ComponentCurves.Num() >= 3)
		{
			Writer.Write(TEXT("cloudColorCurve"), CloudColorCurve);
		}

		if (!Scale.IsNearlyEqual(FGLTFJsonVector3::One, Writer.DefaultTolerance))
		{
			Writer.Write(TEXT("scale"), Scale);
		}
	}
};
