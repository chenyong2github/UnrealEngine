// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RayTracingDefinitions.h"
#include "PathTracingDefinitions.h"

template<typename PassParameterType>
void SetupPathTracingLightParameters(
	const GPULightmass::FLightSceneRenderState& LightScene,
	FRDGBuilder& GraphBuilder,
	PassParameterType* PassParameters)
{
	const uint32 MaxLightCount = RAY_TRACING_LIGHT_COUNT_MAXIMUM;
	FPathTracingLight Lights[RAY_TRACING_LIGHT_COUNT_MAXIMUM] = {};

	unsigned LightCount = 0;

	if (LightScene.SkyLight.IsSet())
	{
		FPathTracingLight& DestLight = Lights[LightCount++];
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
		if (LightCount < MaxLightCount)
		{
			FPathTracingLight& DestLight = Lights[LightCount++];

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
	}

	for (auto Light : LightScene.PointLights.Elements)
	{
		if (LightCount < MaxLightCount)
		{
			FPathTracingLight& DestLight = Lights[LightCount++];

			DestLight.Position = Light.Position;
			DestLight.Color = FVector(Light.Color);
			DestLight.Normal = Light.Direction;
			DestLight.dPdu = FVector::CrossProduct(Light.Tangent, Light.Direction);
			DestLight.dPdv = Light.Tangent;

			DestLight.Dimensions = FVector(Light.SourceRadius, Light.SourceSoftRadius, Light.SourceLength);
			DestLight.Attenuation = 1.0f / Light.AttenuationRadius;
			DestLight.FalloffExponent = Light.FalloffExponent;
			DestLight.IESTextureSlice = -1;

			DestLight.Flags = PATHTRACER_FLAG_TRANSMISSION_MASK;
			DestLight.Flags |= PATHTRACER_FLAG_LIGHTING_CHANNEL_MASK;
			DestLight.Flags |= PATHTRACER_FLAG_CAST_SHADOW_MASK;
			DestLight.Flags |= Light.IsInverseSquared ? 0 : PATHTRACER_FLAG_NON_INVERSE_SQUARE_FALLOFF_MASK;
			DestLight.Flags |= Light.bStationary ? PATHTRACER_FLAG_STATIONARY_MASK : 0;
			DestLight.Flags |= PATHTRACING_LIGHT_POINT;
		}
	}

	for (auto Light : LightScene.SpotLights.Elements)
	{
		if (LightCount < MaxLightCount)
		{
			FPathTracingLight& DestLight = Lights[LightCount++];

			DestLight.Position = Light.Position;
			DestLight.Normal = Light.Direction;
			DestLight.dPdu = FVector::CrossProduct(Light.Tangent, Light.Direction);
			DestLight.dPdv = Light.Tangent;
			DestLight.Color = FVector(Light.Color);
			DestLight.Dimensions = FVector(Light.SourceRadius, Light.SourceSoftRadius, Light.SourceLength);
			DestLight.Shaping = Light.SpotAngles;
			DestLight.Attenuation = 1.0f / Light.AttenuationRadius;
			DestLight.FalloffExponent = Light.FalloffExponent;
			DestLight.IESTextureSlice = -1;

			DestLight.Flags = PATHTRACER_FLAG_TRANSMISSION_MASK;
			DestLight.Flags |= PATHTRACER_FLAG_LIGHTING_CHANNEL_MASK;
			DestLight.Flags |= PATHTRACER_FLAG_CAST_SHADOW_MASK;
			DestLight.Flags |= Light.IsInverseSquared ? 0 : PATHTRACER_FLAG_NON_INVERSE_SQUARE_FALLOFF_MASK;
			DestLight.Flags |= Light.bStationary ? PATHTRACER_FLAG_STATIONARY_MASK : 0;
			DestLight.Flags |= PATHTRACING_LIGHT_SPOT;
		}
	}

	for (auto Light : LightScene.RectLights.Elements)
	{
		if (LightCount < MaxLightCount)
		{
			FPathTracingLight& DestLight = Lights[LightCount++];

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

			DestLight.IESTextureSlice = -1;

			DestLight.Flags = PATHTRACER_FLAG_TRANSMISSION_MASK;
			DestLight.Flags |= PATHTRACER_FLAG_LIGHTING_CHANNEL_MASK;
			DestLight.Flags |= PATHTRACER_FLAG_CAST_SHADOW_MASK;
			DestLight.Flags |= Light.bStationary ? PATHTRACER_FLAG_STATIONARY_MASK : 0;
			DestLight.Flags |= PATHTRACING_LIGHT_RECT;
		}
	}

	{
		// Upload the buffer of lights to the GPU
		size_t DataSize = sizeof(FPathTracingLight) * FMath::Max(LightCount, 1u);
		PassParameters->SceneLights = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(CreateStructuredBuffer(GraphBuilder, TEXT("PathTracingLightsBuffer"), sizeof(FPathTracingLight), FMath::Max(LightCount, 1u), Lights, DataSize)));
		PassParameters->SceneLightCount = LightCount;
	}
}
