// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Json/GLTFJsonObject.h"
#include "Json/GLTFJsonIndex.h"
#include "Json/GLTFJsonColor.h"
#include "Json/GLTFJsonVector.h"

struct GLTFEXPORTER_API FGLTFJsonSkySphereColorCurve : IGLTFJsonArray
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

	virtual void WriteArray(IGLTFJsonWriter& Writer) const override;
};

struct GLTFEXPORTER_API FGLTFJsonSkySphere : IGLTFJsonObject
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

	virtual void WriteObject(IGLTFJsonWriter& Writer) const override;
};
