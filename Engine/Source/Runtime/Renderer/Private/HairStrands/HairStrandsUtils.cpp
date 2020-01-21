// Copyright Epic Games, Inc. All Rights Reserved.

#include "HairStrandsUtils.h"
#include "ScenePrivate.h"
#include "HairStrandsCluster.h"

static float GHairR = 1;
static float GHairTT = 1;
static float GHairTRT = 1;
static float GHairGlobalScattering = 1;
static float GHairLocalScattering = 1;
static FAutoConsoleVariableRef CVarHairR(TEXT("r.HairStrands.Components.R"), GHairR, TEXT("Enable/disable hair BSDF component R"));
static FAutoConsoleVariableRef CVarHairTT(TEXT("r.HairStrands.Components.TT"), GHairTT, TEXT("Enable/disable hair BSDF component TT"));
static FAutoConsoleVariableRef CVarHairTRT(TEXT("r.HairStrands.Components.TRT"), GHairTRT, TEXT("Enable/disable hair BSDF component TRT"));
static FAutoConsoleVariableRef CVarHairGlobalScattering(TEXT("r.HairStrands.Components.GlobalScattering"), GHairGlobalScattering, TEXT("Enable/disable hair BSDF component global scattering"));
static FAutoConsoleVariableRef CVarHairLocalScattering(TEXT("r.HairStrands.Components.LocalScattering"), GHairLocalScattering, TEXT("Enable/disable hair BSDF component local scattering"));

static float GStrandHairRasterizationScale = 0.5f; // For no AA without TAA, a good value is: 1.325f (Empirical)
static FAutoConsoleVariableRef CVarStrandHairRasterizationScale(TEXT("r.HairStrands.RasterizationScale"), GStrandHairRasterizationScale, TEXT("Rasterization scale to snap strand to pixel"), ECVF_Scalability | ECVF_RenderThreadSafe);

static float GStrandHairVelocityRasterizationScale = 1.5f; // Tuned based on heavy motion example (e.g., head shaking)
static FAutoConsoleVariableRef CVarStrandHairMaxRasterizationScale(TEXT("r.HairStrands.VelocityRasterizationScale"), GStrandHairVelocityRasterizationScale, TEXT("Rasterization scale to snap strand to pixel under high velocity"), ECVF_Scalability | ECVF_RenderThreadSafe);

static float GStrandHairShadowRasterizationScale = 1.0f;
static FAutoConsoleVariableRef CVarStrandHairShadowRasterizationScale(TEXT("r.HairStrands.ShadowRasterizationScale"), GStrandHairShadowRasterizationScale, TEXT("Rasterization scale to snap strand to pixel in shadow view"));

static float GDeepShadowAABBScale = 1.0f;
static FAutoConsoleVariableRef CVarDeepShadowAABBScale(TEXT("r.HairStrands.DeepShadow.AABBScale"), GDeepShadowAABBScale, TEXT("Scaling value for loosing/tighting deep shadow bounding volume"));

static int32 GHairVisibilityRectOptimEnable = 0;
static FAutoConsoleVariableRef CVarHairVisibilityRectOptimEnable(TEXT("r.HairStrands.RectLightingOptim"), GHairVisibilityRectOptimEnable, TEXT("Hair Visibility use projected view rect to light only relevant pixels"));

static float GHairDualScatteringRoughnessOverride = 0;
static FAutoConsoleVariableRef CVarHairDualScatteringRoughnessOverride(TEXT("r.HairStrands.DualScatteringRoughness"), GHairDualScatteringRoughnessOverride, TEXT("Override all roughness for the dual scattering evaluation. 0 means no override. Default:0"));
float GetHairDualScatteringRoughnessOverride()
{
	return GHairDualScatteringRoughnessOverride;
}

float SampleCountToSubPixelSize(uint32 SamplePerPixelCount)
{
	float Scale = 1;
	switch (SamplePerPixelCount)
	{
	case 1: Scale = 1.f; break;
	case 4: Scale = 8.f / 16.f; break;
	case 8: Scale = 4.f / 16.f; break;
	}
	return Scale;
}
FHairComponent GetHairComponents()
{
	FHairComponent Out;
	Out.R = GHairR > 0;
	Out.TT = GHairTT > 0;
	Out.TRT = GHairTRT > 0;
	Out.LocalScattering = GHairLocalScattering > 0;
	Out.GlobalScattering = GHairGlobalScattering > 0;
	return Out;
}

uint32 ToBitfield(const FHairComponent& C)
{
	return
		(C.R				? 1u : 0u)      |
		(C.TT				? 1u : 0u) << 1 |
		(C.TRT				? 1u : 0u) << 2 |
		(C.LocalScattering  ? 1u : 0u) << 3 |
		(C.GlobalScattering ? 1u : 0u) << 4 ;
}
FMinHairRadiusAtDepth1 ComputeMinStrandRadiusAtDepth1(
	const FIntPoint& Resolution,
	const float FOV,
	const uint32 SampleCount,
	const float OverrideStrandHairRasterizationScale)
{
	auto InternalMinRadiusAtDepth1 = [Resolution, FOV, SampleCount](float RasterizationScale)
	{
		const float DiameterToRadius = 0.5f;
		const float SubPixelScale = SampleCountToSubPixelSize(SampleCount);
		const float vFOV = FMath::DegreesToRadians(FOV);
		const float StrandDiameterAtDepth1 = FMath::Tan(vFOV * 0.5f) / (0.5f * Resolution.Y) * SubPixelScale;
		return DiameterToRadius * RasterizationScale * StrandDiameterAtDepth1;
	};

	FMinHairRadiusAtDepth1 Out;

	// Scales strand to covers a bit more than a pixel and insure at least one sample point is hit
	const float PrimaryRasterizationScale = OverrideStrandHairRasterizationScale > 0 ? OverrideStrandHairRasterizationScale : GStrandHairRasterizationScale;
	const float VelocityRasterizationScale = OverrideStrandHairRasterizationScale > 0 ? OverrideStrandHairRasterizationScale : GStrandHairVelocityRasterizationScale;
	Out.Primary = InternalMinRadiusAtDepth1(PrimaryRasterizationScale);
	Out.Velocity = InternalMinRadiusAtDepth1(VelocityRasterizationScale);

	return Out;
}

void ComputeWorldToLightClip(
	FMatrix& WorldToClipTransform,
	FMinHairRadiusAtDepth1& MinStrandRadiusAtDepth1,
	const FBoxSphereBounds& PrimitivesBounds,
	const FLightSceneProxy& LightProxy,
	const ELightComponentType LightType,
	const FIntPoint& ShadowResolution)
{
	const FSphere SphereBound = PrimitivesBounds.GetSphere();
	const float SphereRadius = SphereBound.W * GDeepShadowAABBScale;
	const FVector LightPosition = LightProxy.GetPosition();
	const float MinZ = FMath::Max(0.1f, FVector::Distance(LightPosition, SphereBound.Center)) - SphereBound.W;
	const float MaxZ = FMath::Max(0.2f, FVector::Distance(LightPosition, SphereBound.Center)) + SphereBound.W;

	const float StrandHairRasterizationScale = GStrandHairShadowRasterizationScale ? GStrandHairShadowRasterizationScale : GStrandHairRasterizationScale;

	MinStrandRadiusAtDepth1 = FMinHairRadiusAtDepth1();
	WorldToClipTransform = FMatrix::Identity;
	if (LightType == LightType_Directional)
	{
		const FVector LightDirection = LightProxy.GetDirection();
		FReversedZOrthoMatrix OrthoMatrix(SphereRadius, SphereRadius, 1.f / (2 * SphereRadius), 0);
		FLookAtMatrix LookAt(SphereBound.Center - LightDirection * SphereRadius, SphereBound.Center, FVector(0, 0, 1));
		WorldToClipTransform = LookAt * OrthoMatrix;
		MinStrandRadiusAtDepth1.Primary = StrandHairRasterizationScale * SphereRadius / FMath::Min(ShadowResolution.X, ShadowResolution.Y);
		MinStrandRadiusAtDepth1.Velocity = MinStrandRadiusAtDepth1.Primary;
	}
	else if (LightType == LightType_Spot || LightType == LightType_Point)
	{
		const FVector LightDirection = LightPosition - PrimitivesBounds.GetSphere().Center;
		const float SphereDistance = FVector::Distance(LightPosition, SphereBound.Center);
		const float HalfFov = asin(SphereRadius / SphereDistance);

		FReversedZPerspectiveMatrix ProjMatrix(HalfFov, 1, 1, MinZ, MaxZ);
		FLookAtMatrix WorldToLight(LightPosition, SphereBound.Center, FVector(0, 0, 1));
		WorldToClipTransform = WorldToLight * ProjMatrix;
		MinStrandRadiusAtDepth1 = ComputeMinStrandRadiusAtDepth1(ShadowResolution, 2 * HalfFov, 1, StrandHairRasterizationScale);
	}
	else if (LightType == LightType_Rect)
	{
		const FVector LightDirection = LightPosition - PrimitivesBounds.GetSphere().Center;
		const float SphereDistance = FVector::Distance(LightPosition, SphereBound.Center);
		const float HalfFov = asin(SphereRadius / SphereDistance);

		FReversedZPerspectiveMatrix ProjMatrix(HalfFov, 1, 1, MinZ, MaxZ);
		FLookAtMatrix WorldToLight(LightPosition, SphereBound.Center, FVector(0, 0, 1));
		WorldToClipTransform = WorldToLight * ProjMatrix;
		MinStrandRadiusAtDepth1 = ComputeMinStrandRadiusAtDepth1(ShadowResolution, 2 * HalfFov, 1, StrandHairRasterizationScale);
	}
}

FIntRect ComputeProjectedScreenRect(const FBox& B, const FViewInfo& View)
{
	FVector2D MinP( FLT_MAX,  FLT_MAX);
	FVector2D MaxP(-FLT_MAX, -FLT_MAX);
	FVector Vertices[8] =
	{
		FVector(B.Min),
		FVector(B.Min.X, B.Min.Y, B.Max.Z),
		FVector(B.Min.X, B.Max.Y, B.Min.Z),
		FVector(B.Max.X, B.Min.Y, B.Min.Z),
		FVector(B.Max.X, B.Max.Y, B.Min.Z),
		FVector(B.Max.X, B.Min.Y, B.Max.Z),
		FVector(B.Min.X, B.Max.Y, B.Max.Z),
		FVector(B.Max)
	};

	for (uint32 i = 0; i < 8; ++i)
	{
		FVector2D P;
		if (View.WorldToPixel(Vertices[i], P))
		{
			MinP.X = FMath::Min(MinP.X, P.X);
			MinP.Y = FMath::Min(MinP.Y, P.Y);
			MaxP.X = FMath::Max(MaxP.X, P.X);
			MaxP.Y = FMath::Max(MaxP.Y, P.Y);
		}
	}

	FIntRect OutRect;
	OutRect.Min = FIntPoint(FMath::FloorToInt(MinP.X), FMath::FloorToInt(MinP.Y));
	OutRect.Max = FIntPoint(FMath::CeilToInt(MaxP.X),  FMath::CeilToInt(MaxP.Y));

	// Clamp to screen rect
	OutRect.Min.X = FMath::Max(View.ViewRect.Min.X, OutRect.Min.X);
	OutRect.Min.Y = FMath::Max(View.ViewRect.Min.Y, OutRect.Min.Y);
	OutRect.Max.X = FMath::Min(View.ViewRect.Max.X, OutRect.Max.X);
	OutRect.Max.Y = FMath::Min(View.ViewRect.Max.Y, OutRect.Max.Y);

	return OutRect;
}


FIntRect ComputeVisibleHairStrandsMacroGroupsRect(const FIntRect& ViewRect, const FHairStrandsMacroGroupDatas& Datas)
{
	FIntRect TotalRect(INT_MAX, INT_MAX, -INT_MAX, -INT_MAX);
	if (IsHairStrandsViewRectOptimEnable())
	{
		for (const FHairStrandsMacroGroupData& Data : Datas.Datas)
		{
			TotalRect.Union(Data.ScreenRect);
		}
	}
	else
	{
		TotalRect = ViewRect;
	}

	return TotalRect;
}

bool IsHairStrandsViewRectOptimEnable()
{
	return GHairVisibilityRectOptimEnable > 0;
}

bool IsHairStrandsSupported(const EShaderPlatform Platform)
{
	return IsD3DPlatform(Platform, false) && IsPCPlatform(Platform) && GetMaxSupportedFeatureLevel(Platform) == ERHIFeatureLevel::SM5;
}

EHairVisibilityVendor GetVendor()
{
	return IsRHIDeviceAMD() ? HairVisibilityVendor_AMD : (IsRHIDeviceNVIDIA() ? HairVisibilityVendor_NVIDIA : HairVisibilityVendor_INTEL);
}

uint32 GetVendorOptimalGroupSize1D()
{
	switch (GetVendor())
	{
	case HairVisibilityVendor_AMD:		return 64;
	case HairVisibilityVendor_NVIDIA:	return 32;
	case HairVisibilityVendor_INTEL:	return 64;
	default:							return 64;
	}
}

FIntPoint GetVendorOptimalGroupSize2D()
{
	switch (GetVendor())
	{
	case HairVisibilityVendor_AMD:		return FIntPoint(8, 8);
	case HairVisibilityVendor_NVIDIA:	return FIntPoint(8, 4);
	case HairVisibilityVendor_INTEL:	return FIntPoint(8, 8);
	default:							return FIntPoint(8, 8);
	}
}

FVector4 PackHairRenderInfo(
	float PrimaryRadiusAtDepth1,
	float VelocityRadiusAtDepth1,
	float VelocityMagnitudeScale,
	bool bIsOrtho,
	bool bIsGPUDriven)
{
	uint32 BitField = 0;
	BitField |= bIsOrtho ? 0x1 : 0;
	BitField |= bIsGPUDriven ? 0x2 : 0;

	FVector4 Out;
	Out.X = PrimaryRadiusAtDepth1;
	Out.Y = VelocityRadiusAtDepth1;
	Out.Z = VelocityMagnitudeScale;
	Out.W = *((float*)(&BitField));

	return Out;
}