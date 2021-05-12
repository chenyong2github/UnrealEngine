// Copyright Epic Games, Inc. All Rights Reserved.

#include "LumenHardwareRayTracingCommon.h"
#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "LumenSceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"

static TAutoConsoleVariable<int32> CVarLumenUseHardwareRayTracing(
	TEXT("r.Lumen.HardwareRayTracing"),
	0,
	TEXT("Uses Hardware Ray Tracing for Lumen features, when available.\n")
	TEXT("Lumen will fall back to Software Ray Tracing otherwise.\n")
	TEXT("Note: Hardware ray tracing has significant scene update costs for\n")
	TEXT("scenes with more than 10k instances."),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarLumenHardwareRayTracingPullbackBias(
	TEXT("r.Lumen.HardwareRayTracing.PullbackBias"),
	8.0,
	TEXT("Determines the pull-back bias when resuming a screen-trace ray (default = 8.0)"),
	ECVF_RenderThreadSafe
);

#if RHI_RAYTRACING

#include "LumenHardwareRayTracingCommon.h"

namespace Lumen
{
	bool UseHardwareRayTracing()
	{
#if RHI_RAYTRACING
		return (IsRayTracingEnabled() && CVarLumenUseHardwareRayTracing.GetValueOnRenderThread() != 0);
#else
		return false;
#endif
	}

	const TCHAR* GetRayTracedNormalModeName(int NormalMode)
	{
		if (NormalMode == 0)
		{
			return TEXT("SDF");
		}

		return TEXT("Geometry");
	}

	float GetHardwareRayTracingPullbackBias()
	{
		return CVarLumenHardwareRayTracingPullbackBias.GetValueOnRenderThread();
	}
}

void SetLumenHardwareRayTracingSharedParameters(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View,
	const FLumenCardTracingInputs& TracingInputs,
	FLumenHardwareRayTracingRGS::FSharedParameters* SharedParameters
)
{
	SharedParameters->SceneTextures = SceneTextures;
	//SharedParameters->ViewUniformBuffer = View.ViewUniformBuffer;
	checkf(View.HasRayTracingScene(), TEXT("TLAS does not exist. Verify that the current pass is represented in Lumen::AnyLumenHardwareRayTracingPassEnabled()."));
	SharedParameters->TLAS = View.GetRayTracingSceneViewChecked();

	// Lighting data
	SharedParameters->LightDataPacked = View.RayTracingLightData.UniformBuffer;
	SharedParameters->LightDataBuffer = View.RayTracingLightData.LightBufferSRV;
	SharedParameters->SSProfilesTexture = GraphBuilder.RegisterExternalTexture(View.RayTracingSubSurfaceProfileTexture);

	// Use surface cache, instead
	GetLumenCardTracingParameters(View, TracingInputs, SharedParameters->TracingParameters);
}

void SetupRGSRadianceCacheParameters(const LumenRadianceCache::FRadianceCacheInterpolationParameters& RadianceCacheParameters,
	FRGSRadianceCacheParameters& RGSRadianceCacheParameters)
{
	for (int i = 0; i < LumenRadianceCache::MaxClipmaps; ++i)
	{
		RGSRadianceCacheParameters.RadianceProbeClipmapTMin[i] = RadianceCacheParameters.RadianceProbeClipmapTMin[i];
		RGSRadianceCacheParameters.RadianceProbeClipmapSamplingJitter[i] = RadianceCacheParameters.RadianceProbeClipmapSamplingJitter[i];
		RGSRadianceCacheParameters.WorldPositionToRadianceProbeCoordScale[i] = RadianceCacheParameters.WorldPositionToRadianceProbeCoordScale[i];
		RGSRadianceCacheParameters.WorldPositionToRadianceProbeCoordBias[i] = RadianceCacheParameters.WorldPositionToRadianceProbeCoordBias[i];
		RGSRadianceCacheParameters.RadianceProbeCoordToWorldPositionScale[i] = RadianceCacheParameters.RadianceProbeCoordToWorldPositionScale[i];
		RGSRadianceCacheParameters.RadianceProbeCoordToWorldPositionBias[i] = RadianceCacheParameters.RadianceProbeCoordToWorldPositionBias[i];
	}
}

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FRGSRadianceCacheParameters, "RGSRadianceCacheParameters");

#endif // RHI_RAYTRACING