// Copyright Epic Games, Inc. All Rights Reserved.

#include "Lights.h"
#include "RenderGraphBuilder.h"
#include "ReflectionEnvironment.h"

RENDERER_API void PrepareSkyTexture_Internal(
	FRDGBuilder& GraphBuilder,
	FReflectionUniformParameters& Parameters,
	uint32 Size,
	FLinearColor SkyColor,
	bool UseMISCompensation,

	// Out
	FRDGTextureRef& SkylightTexture,
	FRDGTextureRef& SkylightPdf,
	float& SkylightInvResolution,
	int32& SkylightMipCount
);

namespace GPULightmass
{

void FLightBuildInfoRef::RemoveFromAray()
{
	LightArray.Remove(*this);

	check(!IsValid());
}

FLocalLightBuildInfo& FLightBuildInfoRef::Resolve()
{
	return LightArray.ResolveAsLocalLightBuildInfo(*this);
}

FLocalLightRenderState& FLightRenderStateRef::Resolve()
{
	return LightRenderStateArray.ResolveAsLocalLightRenderState(*this);
}

FDirectionalLightBuildInfo::FDirectionalLightBuildInfo(UDirectionalLightComponent* DirectionalLightComponent)
{
	const bool bCastStationaryShadows = DirectionalLightComponent->CastShadows && DirectionalLightComponent->CastStaticShadows && !DirectionalLightComponent->HasStaticLighting();

	ComponentUObject = DirectionalLightComponent;
	bStationary = bCastStationaryShadows;
	ShadowMapChannel = DirectionalLightComponent->PreviewShadowMapChannel;
	LightComponentMapBuildData = MakeUnique<FLightComponentMapBuildData>();
	LightComponentMapBuildData->ShadowMapChannel = DirectionalLightComponent->PreviewShadowMapChannel;
}

FDirectionalLightRenderState::FDirectionalLightRenderState(UDirectionalLightComponent* DirectionalLightComponent)
{
	const bool bCastStationaryShadows = DirectionalLightComponent->CastShadows && DirectionalLightComponent->CastStaticShadows && !DirectionalLightComponent->HasStaticLighting();

	bStationary = bCastStationaryShadows;
	Color = DirectionalLightComponent->GetColoredLightBrightness();
	Direction = DirectionalLightComponent->GetDirection();
	LightSourceAngle = DirectionalLightComponent->LightSourceAngle;
	LightSourceSoftAngle = DirectionalLightComponent->LightSourceSoftAngle;
	ShadowMapChannel = DirectionalLightComponent->PreviewShadowMapChannel;
}

FPointLightBuildInfo::FPointLightBuildInfo(UPointLightComponent* PointLightComponent)
{
	const bool bCastStationaryShadows = PointLightComponent->CastShadows && PointLightComponent->CastStaticShadows && !PointLightComponent->HasStaticLighting();

	ComponentUObject = PointLightComponent;
	bStationary = bCastStationaryShadows;
	ShadowMapChannel = PointLightComponent->PreviewShadowMapChannel;
	LightComponentMapBuildData = MakeUnique<FLightComponentMapBuildData>();
	LightComponentMapBuildData->ShadowMapChannel = PointLightComponent->PreviewShadowMapChannel;
	Position = PointLightComponent->GetLightPosition();
	AttenuationRadius = PointLightComponent->AttenuationRadius;
}

bool FPointLightBuildInfo::AffectsBounds(const FBoxSphereBounds& InBounds) const
{
	return (InBounds.Origin - Position).SizeSquared() <= FMath::Square(AttenuationRadius + InBounds.SphereRadius);
}

FPointLightRenderState::FPointLightRenderState(UPointLightComponent* PointLightComponent)
{
	const bool bCastStationaryShadows = PointLightComponent->CastShadows && PointLightComponent->CastStaticShadows && !PointLightComponent->HasStaticLighting();

	bStationary = bCastStationaryShadows;
	Color = PointLightComponent->GetColoredLightBrightness();
	Position = PointLightComponent->GetLightPosition();
	Direction = PointLightComponent->GetDirection();
	{
		FMatrix LightToWorld = PointLightComponent->GetComponentTransform().ToMatrixNoScale();
		Tangent = FVector(LightToWorld.M[2][0], LightToWorld.M[2][1], LightToWorld.M[2][2]);
	}
	AttenuationRadius = PointLightComponent->AttenuationRadius;
	SourceRadius = PointLightComponent->SourceRadius;
	SourceSoftRadius = PointLightComponent->SoftSourceRadius;
	SourceLength = PointLightComponent->SourceLength;
	ShadowMapChannel = PointLightComponent->PreviewShadowMapChannel;
	FalloffExponent = PointLightComponent->LightFalloffExponent;
	IsInverseSquared = PointLightComponent->bUseInverseSquaredFalloff;
	IESTexture = PointLightComponent->IESTexture ? PointLightComponent->IESTexture->GetResource() : nullptr;
}

FSpotLightBuildInfo::FSpotLightBuildInfo(USpotLightComponent* SpotLightComponent)
{
	const bool bCastStationaryShadows = SpotLightComponent->CastShadows && SpotLightComponent->CastStaticShadows && !SpotLightComponent->HasStaticLighting();

	ComponentUObject = SpotLightComponent;
	bStationary = bCastStationaryShadows;
	ShadowMapChannel = SpotLightComponent->PreviewShadowMapChannel;
	LightComponentMapBuildData = MakeUnique<FLightComponentMapBuildData>();
	LightComponentMapBuildData->ShadowMapChannel = SpotLightComponent->PreviewShadowMapChannel;
	Position = SpotLightComponent->GetLightPosition();
	Direction = SpotLightComponent->GetDirection();
	AttenuationRadius = SpotLightComponent->AttenuationRadius;
	InnerConeAngle = SpotLightComponent->InnerConeAngle;
	OuterConeAngle = SpotLightComponent->OuterConeAngle;
}

bool FSpotLightBuildInfo::AffectsBounds(const FBoxSphereBounds& InBounds) const
{
	if ((InBounds.Origin - Position).SizeSquared() <= FMath::Square(AttenuationRadius + InBounds.SphereRadius))
	{
		float ClampedInnerConeAngle = FMath::Clamp(InnerConeAngle, 0.0f, 89.0f) * (float)PI / 180.0f;
		float ClampedOuterConeAngle = FMath::Clamp(OuterConeAngle * (float)PI / 180.0f, ClampedInnerConeAngle + 0.001f, 89.0f * (float)PI / 180.0f + 0.001f);

		float Sin = FMath::Sin(ClampedOuterConeAngle);
		float Cos = FMath::Cos(ClampedOuterConeAngle);

		FVector	U = Position - (InBounds.SphereRadius / Sin) * Direction;
		FVector	D = InBounds.Origin - U;
		float dsqr = D | D;
		float E = Direction | D;
		if (E > 0.0f && E * E >= dsqr * FMath::Square(Cos))
		{
			D = InBounds.Origin - Position;
			dsqr = D | D;
			E = -(Direction | D);
			if (E > 0.0f && E * E >= dsqr * FMath::Square(Sin))
				return dsqr <= FMath::Square(InBounds.SphereRadius);
			else
				return true;
		}
	}

	return false;
}

FSpotLightRenderState::FSpotLightRenderState(USpotLightComponent* SpotLightComponent)
{
	const bool bCastStationaryShadows = SpotLightComponent->CastShadows && SpotLightComponent->CastStaticShadows && !SpotLightComponent->HasStaticLighting();

	bStationary = bCastStationaryShadows;
	Color = SpotLightComponent->GetColoredLightBrightness();
	Position = SpotLightComponent->GetLightPosition();
	Direction = SpotLightComponent->GetDirection();
	{
		const float ClampedInnerConeAngle = FMath::Clamp(SpotLightComponent->InnerConeAngle, 0.0f, 89.0f) * (float)PI / 180.0f;
		const float ClampedOuterConeAngle = FMath::Clamp(SpotLightComponent->OuterConeAngle * (float)PI / 180.0f, ClampedInnerConeAngle + 0.001f, 89.0f * (float)PI / 180.0f + 0.001f);
		const float CosOuterCone = FMath::Cos(ClampedOuterConeAngle);
		const float CosInnerCone = FMath::Cos(ClampedInnerConeAngle);
		const float InvCosConeDifference = 1.0f / (CosInnerCone - CosOuterCone);
		SpotAngles = FVector2D(CosOuterCone, InvCosConeDifference);
	}
	{
		FMatrix LightToWorld = SpotLightComponent->GetComponentTransform().ToMatrixNoScale();
		Tangent = FVector(LightToWorld.M[2][0], LightToWorld.M[2][1], LightToWorld.M[2][2]);
	}
	AttenuationRadius = SpotLightComponent->AttenuationRadius;
	SourceRadius = SpotLightComponent->SourceRadius;
	SourceSoftRadius = SpotLightComponent->SoftSourceRadius;
	SourceLength = SpotLightComponent->SourceLength;
	ShadowMapChannel = SpotLightComponent->PreviewShadowMapChannel;
	FalloffExponent = SpotLightComponent->LightFalloffExponent;
	IsInverseSquared = SpotLightComponent->bUseInverseSquaredFalloff;
	IESTexture = SpotLightComponent->IESTexture ? SpotLightComponent->IESTexture->GetResource() : nullptr;
}

FRectLightBuildInfo::FRectLightBuildInfo(URectLightComponent* RectLightComponent)
{
	const bool bCastStationaryShadows = RectLightComponent->CastShadows && RectLightComponent->CastStaticShadows && !RectLightComponent->HasStaticLighting();

	ComponentUObject = RectLightComponent;
	bStationary = bCastStationaryShadows;
	ShadowMapChannel = RectLightComponent->PreviewShadowMapChannel;
	LightComponentMapBuildData = MakeUnique<FLightComponentMapBuildData>();
	LightComponentMapBuildData->ShadowMapChannel = RectLightComponent->PreviewShadowMapChannel;
	Position = RectLightComponent->GetLightPosition();
	AttenuationRadius = RectLightComponent->AttenuationRadius;
}

bool FRectLightBuildInfo::AffectsBounds(const FBoxSphereBounds& InBounds) const
{
	return (InBounds.Origin - Position).SizeSquared() <= FMath::Square(AttenuationRadius + InBounds.SphereRadius);
}

FRectLightRenderState::FRectLightRenderState(URectLightComponent* RectLightComponent)
{
	const bool bCastStationaryShadows = RectLightComponent->CastShadows && RectLightComponent->CastStaticShadows && !RectLightComponent->HasStaticLighting();

	bStationary = bCastStationaryShadows;
	Color = RectLightComponent->GetColoredLightBrightness();
	Position = RectLightComponent->GetLightPosition();
	Direction = RectLightComponent->GetDirection();
	{
		FMatrix LightToWorld = RectLightComponent->GetComponentTransform().ToMatrixNoScale();
		Tangent = FVector(LightToWorld.M[2][0], LightToWorld.M[2][1], LightToWorld.M[2][2]);
	}
	SourceWidth = RectLightComponent->SourceWidth;
	SourceHeight = RectLightComponent->SourceHeight;
	BarnDoorAngle = FMath::Clamp(RectLightComponent->BarnDoorAngle, 0.f, GetRectLightBarnDoorMaxAngle());
	BarnDoorLength = FMath::Max(0.1f, RectLightComponent->BarnDoorLength);
	AttenuationRadius = RectLightComponent->AttenuationRadius;
	ShadowMapChannel = RectLightComponent->PreviewShadowMapChannel;
	IESTexture = RectLightComponent->IESTexture ? RectLightComponent->IESTexture->GetResource() : nullptr;
}

FLightShaderParameters FDirectionalLightRenderState::GetLightShaderParameters() const
{
	FLightShaderParameters LightParameters;

	LightParameters.Position = FVector::ZeroVector;
	LightParameters.InvRadius = 0.0f;
	// TODO: support SkyAtmosphere
	// LightParameters.Color = FVector(GetColor() * AtmosphereTransmittanceFactor);
	LightParameters.Color = FVector(Color);
	LightParameters.FalloffExponent = 0.0f;

	LightParameters.Direction = -Direction;
	LightParameters.Tangent = -Direction;

	LightParameters.SpotAngles = FVector2D(0, 0);
	LightParameters.SpecularScale = 0; // Irrelevant when tracing shadow rays
	LightParameters.SourceRadius = FMath::Sin(0.5f * FMath::DegreesToRadians(LightSourceAngle));
	LightParameters.SoftSourceRadius = 0; // Irrelevant when tracing shadow rays. FMath::Sin(0.5f * FMath::DegreesToRadians(LightSourceSoftAngle));
	LightParameters.SourceLength = 0.0f;
	LightParameters.SourceTexture = GWhiteTexture->TextureRHI; // Irrelevant when tracing shadow rays

	return LightParameters;
}

FLightShaderParameters FPointLightRenderState::GetLightShaderParameters() const
{
	FLightShaderParameters LightParameters;

	LightParameters.Position = Position;
	LightParameters.InvRadius = 1.0f / AttenuationRadius;
	LightParameters.Color = FVector(Color);
	LightParameters.SourceRadius = SourceRadius;

	return LightParameters;
}

FLightShaderParameters FSpotLightRenderState::GetLightShaderParameters() const
{
	FLightShaderParameters LightParameters;

	LightParameters.Position = Position;
	LightParameters.Direction = -Direction;
	LightParameters.Tangent = Tangent;
	LightParameters.SpotAngles = SpotAngles;
	LightParameters.InvRadius = 1.0f / AttenuationRadius;
	LightParameters.Color = FVector(Color);
	LightParameters.SourceRadius = SourceRadius;

	return LightParameters;
}

FLightShaderParameters FRectLightRenderState::GetLightShaderParameters() const
{
	FLightShaderParameters LightParameters;

	LightParameters.Position = Position;
	LightParameters.Direction = -Direction;
	LightParameters.Tangent = Tangent;
	LightParameters.InvRadius = 1.0f / AttenuationRadius;

	FLinearColor LightColor = Color;
	LightColor /= 0.5f * SourceWidth * SourceHeight;
	LightParameters.Color = FVector(LightColor);

	LightParameters.SourceRadius = SourceWidth * 0.5f;
	LightParameters.SourceLength = SourceHeight * 0.5f;
	LightParameters.SourceTexture =  GWhiteTexture->TextureRHI;
	LightParameters.RectLightBarnCosAngle = FMath::Cos(FMath::DegreesToRadians(BarnDoorAngle));
	LightParameters.RectLightBarnLength = BarnDoorLength;

	return LightParameters;
}

void FSkyLightRenderState::PrepareSkyTexture(FRHICommandListImmediate& RHICmdList)
{
	FRDGBuilder GraphBuilder(RHICmdList);

	FRDGTextureRef SkylightTexture;
	FRDGTextureRef SkylightPdf;

	FReflectionUniformParameters Parameters;
	Parameters.SkyLightCubemap = ProcessedTexture;
	Parameters.SkyLightCubemapSampler = ProcessedTextureSampler;
	Parameters.SkyLightBlendDestinationCubemap = ProcessedTexture;
	Parameters.SkyLightBlendDestinationCubemapSampler = ProcessedTextureSampler;
	Parameters.SkyLightParameters = FVector4(1, 1, 0, 0);

	FLinearColor SkyColor = Color;
	// since we are resampled into an octahedral layout, we multiply the cubemap resolution by 2 to get roughly the same number of texels
	uint32 Size = FMath::RoundUpToPowerOfTwo(2 * TextureDimensions.X);

	const bool UseMISCompensation = true;

	PrepareSkyTexture_Internal(
		GraphBuilder,
		Parameters,
		Size,
		SkyColor,
		UseMISCompensation,
		// Out
		SkylightTexture,
		SkylightPdf,
		SkylightInvResolution,
		SkylightMipCount
	);

	GraphBuilder.QueueTextureExtraction(SkylightTexture, &PathTracingSkylightTexture);
	GraphBuilder.QueueTextureExtraction(SkylightPdf, &PathTracingSkylightPdf);

	GraphBuilder.Execute();
}

}
