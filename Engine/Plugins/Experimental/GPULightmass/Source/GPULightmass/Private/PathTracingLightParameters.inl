// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RayTracingDefinitions.h"
#include "PathTracingDefinitions.h"

RENDERER_API FRDGTexture* PrepareIESAtlas(const TMap<FTexture*, int>& InIESLightProfilesMap, FRDGBuilder& GraphBuilder);

RENDERER_API void PrepareLightGrid(FRDGBuilder& GraphBuilder, FPathTracingLightGrid* LightGridParameters, const FPathTracingLight* Lights, uint32 NumLights, uint32 NumInfiniteLights, FRDGBufferSRV* LightsSRV);

template<typename PassParameterType>
void SetupPathTracingLightParameters(
	const GPULightmass::FLightSceneRenderState& LightScene,
	FRDGBuilder& GraphBuilder,
	PassParameterType* PassParameters)
{
	TArray<FPathTracingLight> Lights;

	if (LightScene.SkyLight.IsSet())
	{
		FPathTracingLight& DestLight = Lights.AddDefaulted_GetRef();
		DestLight.Color = FVector(LightScene.SkyLight->Color);
		DestLight.Flags = PATHTRACER_FLAG_TRANSMISSION_MASK;
		DestLight.Flags |= PATHTRACER_FLAG_LIGHTING_CHANNEL_MASK;
		DestLight.Flags |= PATHTRACER_FLAG_CAST_SHADOW_MASK;
		bool SkyLightIsStationary = LightScene.SkyLight->bStationary;
		DestLight.Flags |= SkyLightIsStationary ? PATHTRACER_FLAG_STATIONARY_MASK : 0;
		DestLight.Flags |= PATHTRACING_LIGHT_SKY;

		PassParameters->SkylightTexture = GraphBuilder.RegisterExternalTexture(LightScene.SkyLight->PathTracingSkylightTexture, TEXT("PathTracer.Skylight"));
		PassParameters->SkylightTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->SkylightPdf = GraphBuilder.RegisterExternalTexture(LightScene.SkyLight->PathTracingSkylightPdf, TEXT("PathTracer.SkylightPdf"));
		PassParameters->SkylightInvResolution = LightScene.SkyLight->SkylightInvResolution;
		PassParameters->SkylightMipCount = LightScene.SkyLight->SkylightMipCount;
	}
	else
	{
		PassParameters->SkylightTexture = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
		PassParameters->SkylightTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->SkylightPdf = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
		PassParameters->SkylightInvResolution = 0;
		PassParameters->SkylightMipCount = 0;
	}


	for (auto Light : LightScene.DirectionalLights.Elements)
	{
		FPathTracingLight& DestLight = Lights.AddDefaulted_GetRef();

		DestLight.Normal = -Light.Direction;
		DestLight.Color = FVector(Light.Color);
		DestLight.Dimensions = FVector(
			FMath::Sin(0.5f * FMath::DegreesToRadians(Light.LightSourceAngle)),
			FMath::Sin(0.5f * FMath::DegreesToRadians(Light.LightSourceSoftAngle)),
			0.0f);
		DestLight.Attenuation = 1.0;
		DestLight.IESTextureSlice = -1;

		DestLight.Flags = PATHTRACER_FLAG_TRANSMISSION_MASK;
		DestLight.Flags |= PATHTRACER_FLAG_LIGHTING_CHANNEL_MASK;
		DestLight.Flags |= PATHTRACER_FLAG_CAST_SHADOW_MASK;
		DestLight.Flags |= Light.bStationary ? PATHTRACER_FLAG_STATIONARY_MASK : 0;
		DestLight.Flags |= PATHTRACING_LIGHT_DIRECTIONAL;
	}

	uint32 NumInfiniteLights = Lights.Num();

	TMap<FTexture*, int> IESLightProfilesMap;

	for (auto Light : LightScene.PointLights.Elements)
	{
		FPathTracingLight& DestLight = Lights.AddDefaulted_GetRef();

		DestLight.Position = Light.Position;
		DestLight.Color = FVector(Light.Color);
		DestLight.Normal = FVector(1, 0, 0);
		DestLight.dPdu = FVector::CrossProduct(Light.Tangent, Light.Direction);
		DestLight.dPdv = Light.Tangent;

		DestLight.Dimensions = FVector(Light.SourceRadius, Light.SourceSoftRadius, Light.SourceLength);
		DestLight.Attenuation = 1.0f / Light.AttenuationRadius;
		DestLight.FalloffExponent = Light.FalloffExponent;

		if (Light.IESTexture)
		{
			DestLight.IESTextureSlice = IESLightProfilesMap.FindOrAdd(Light.IESTexture, IESLightProfilesMap.Num());
		}
		else
		{
			DestLight.IESTextureSlice = INDEX_NONE;
		}

		DestLight.Flags = PATHTRACER_FLAG_TRANSMISSION_MASK;
		DestLight.Flags |= PATHTRACER_FLAG_LIGHTING_CHANNEL_MASK;
		DestLight.Flags |= PATHTRACER_FLAG_CAST_SHADOW_MASK;
		DestLight.Flags |= Light.IsInverseSquared ? 0 : PATHTRACER_FLAG_NON_INVERSE_SQUARE_FALLOFF_MASK;
		DestLight.Flags |= Light.bStationary ? PATHTRACER_FLAG_STATIONARY_MASK : 0;
		DestLight.Flags |= PATHTRACING_LIGHT_POINT;

		float Radius = Light.AttenuationRadius;
		FVector Center = DestLight.Position;
		// simple sphere of influence
		DestLight.BoundMin = Center - FVector(Radius, Radius, Radius);
		DestLight.BoundMax = Center + FVector(Radius, Radius, Radius);
	}

	for (auto Light : LightScene.SpotLights.Elements)
	{
		FPathTracingLight& DestLight = Lights.AddDefaulted_GetRef();

		DestLight.Position = Light.Position;
		DestLight.Normal = Light.Direction;
		DestLight.dPdu = FVector::CrossProduct(Light.Tangent, Light.Direction);
		DestLight.dPdv = Light.Tangent;
		DestLight.Color = FVector(Light.Color);
		DestLight.Dimensions = FVector(Light.SourceRadius, Light.SourceSoftRadius, Light.SourceLength);
		DestLight.Shaping = Light.SpotAngles;
		DestLight.Attenuation = 1.0f / Light.AttenuationRadius;
		DestLight.FalloffExponent = Light.FalloffExponent;

		if (Light.IESTexture)
		{
			DestLight.IESTextureSlice = IESLightProfilesMap.FindOrAdd(Light.IESTexture, IESLightProfilesMap.Num());
		}
		else
		{
			DestLight.IESTextureSlice = INDEX_NONE;
		}

		DestLight.Flags = PATHTRACER_FLAG_TRANSMISSION_MASK;
		DestLight.Flags |= PATHTRACER_FLAG_LIGHTING_CHANNEL_MASK;
		DestLight.Flags |= PATHTRACER_FLAG_CAST_SHADOW_MASK;
		DestLight.Flags |= Light.IsInverseSquared ? 0 : PATHTRACER_FLAG_NON_INVERSE_SQUARE_FALLOFF_MASK;
		DestLight.Flags |= Light.bStationary ? PATHTRACER_FLAG_STATIONARY_MASK : 0;
		DestLight.Flags |= PATHTRACING_LIGHT_SPOT;

		float Radius = Light.AttenuationRadius;
		FVector Center = DestLight.Position;
		FVector Normal = DestLight.Normal;
		FVector Disc = FVector(
			FMath::Sqrt(FMath::Clamp(1 - Normal.X * Normal.X, 0.0f, 1.0f)),
			FMath::Sqrt(FMath::Clamp(1 - Normal.Y * Normal.Y, 0.0f, 1.0f)),
			FMath::Sqrt(FMath::Clamp(1 - Normal.Z * Normal.Z, 0.0f, 1.0f))
		);
		// box around ray from light center to tip of the cone
		FVector Tip = Center + Normal * Radius;
		DestLight.BoundMin = Center.ComponentMin(Tip);
		DestLight.BoundMax = Center.ComponentMax(Tip);
		// expand by disc around the farthest part of the cone

		float CosOuter = Light.SpotAngles.X;
		float SinOuter = FMath::Sqrt(1.0f - CosOuter * CosOuter);

		DestLight.BoundMin = DestLight.BoundMin.ComponentMin(Center + Radius * (Normal * CosOuter - Disc * SinOuter));
		DestLight.BoundMax = DestLight.BoundMax.ComponentMax(Center + Radius * (Normal * CosOuter + Disc * SinOuter));
	}

	for (auto Light : LightScene.RectLights.Elements)
	{
		FPathTracingLight& DestLight = Lights.AddDefaulted_GetRef();

		DestLight.Position = Light.Position;
		DestLight.Normal = Light.Direction;
		DestLight.dPdu = FVector::CrossProduct(Light.Tangent, -Light.Direction);
		DestLight.dPdv = Light.Tangent;

		FLinearColor LightColor = Light.Color;
		LightColor /= 0.5f * Light.SourceWidth * Light.SourceHeight;
		DestLight.Color = FVector(LightColor);

		DestLight.Dimensions = FVector(Light.SourceWidth, Light.SourceHeight, 0.0f);
		DestLight.Attenuation = 1.0f / Light.AttenuationRadius;
		DestLight.Shaping = FVector2D(FMath::Cos(FMath::DegreesToRadians(Light.BarnDoorAngle)), Light.BarnDoorLength);

		if (Light.IESTexture)
		{
			DestLight.IESTextureSlice = IESLightProfilesMap.FindOrAdd(Light.IESTexture, IESLightProfilesMap.Num());
		}
		else
		{
			DestLight.IESTextureSlice = INDEX_NONE;
		}

		DestLight.Flags = PATHTRACER_FLAG_TRANSMISSION_MASK;
		DestLight.Flags |= PATHTRACER_FLAG_LIGHTING_CHANNEL_MASK;
		DestLight.Flags |= PATHTRACER_FLAG_CAST_SHADOW_MASK;
		DestLight.Flags |= Light.bStationary ? PATHTRACER_FLAG_STATIONARY_MASK : 0;
		DestLight.Flags |= PATHTRACING_LIGHT_RECT;

		float Radius = Light.AttenuationRadius;
		FVector Center = DestLight.Position;
		FVector Normal = DestLight.Normal;
		FVector Disc = FVector(
			FMath::Sqrt(FMath::Clamp(1 - Normal.X * Normal.X, 0.0f, 1.0f)),
			FMath::Sqrt(FMath::Clamp(1 - Normal.Y * Normal.Y, 0.0f, 1.0f)),
			FMath::Sqrt(FMath::Clamp(1 - Normal.Z * Normal.Z, 0.0f, 1.0f))
		);
		// quad bbox is the bbox of the disc +  the tip of the hemisphere
		// TODO: is it worth trying to account for barndoors? seems unlikely to cut much empty space since the volume _inside_ the barndoor receives light
		FVector Tip = Center + Normal * Radius;
		DestLight.BoundMin = Tip.ComponentMin(Center - Radius * Disc);
		DestLight.BoundMax = Tip.ComponentMax(Center + Radius * Disc);
	}

	PassParameters->SceneLightCount = Lights.Num();
	{
		// Upload the buffer of lights to the GPU
		// need at least one since zero-sized buffers are not allowed
		if (Lights.Num() == 0)
		{
			Lights.AddDefaulted();
		}
		PassParameters->SceneLights = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(CreateStructuredBuffer(GraphBuilder, TEXT("PathTracer.LightsBuffer"), sizeof(FPathTracingLight), Lights.Num(), Lights.GetData(), sizeof(FPathTracingLight) * Lights.Num())));
	}

	if (IESLightProfilesMap.Num() > 0)
	{
		PassParameters->IESTexture = PrepareIESAtlas(IESLightProfilesMap, GraphBuilder);
	}
	else
	{
		PassParameters->IESTexture = GraphBuilder.RegisterExternalTexture(GSystemTextures.WhiteDummy);
	}

	PrepareLightGrid(GraphBuilder, &PassParameters->LightGridParameters, Lights.GetData(), PassParameters->SceneLightCount, NumInfiniteLights, PassParameters->SceneLights);
}
