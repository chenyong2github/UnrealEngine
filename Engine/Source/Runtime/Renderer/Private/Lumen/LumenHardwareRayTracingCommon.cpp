// Copyright Epic Games, Inc. All Rights Reserved.

#if RHI_RAYTRACING

#include "LumenHardwareRayTracingCommon.h"

static TAutoConsoleVariable<int32> CVarLumenUseHardwareRayTracing(
	TEXT("r.Lumen.HardwareRayTracing"),
	0,
	TEXT("Uses Hardware Ray Tracing for Lumen features, when available.\n")
	TEXT("Lumen will fall back to Software Ray Tracing otherwise.\n")
	TEXT("Note: Hardware ray tracing has significant scene update costs for\n")
	TEXT("scenes with more than 10k instances."),
	ECVF_RenderThreadSafe
);

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
}

void SetLumenHardwareRayTracingSharedParameters(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View,
	const FLumenCardTracingInputs& TracingInputs,
	const FLumenMeshSDFGridParameters& MeshSDFGridParameters,
	FLumenHardwareRayTracingRGS::FSharedParameters* SharedParameters
)
{
	SharedParameters->SceneTextures = SceneTextures;
	//SharedParameters->ViewUniformBuffer = View.ViewUniformBuffer;
	checkf(View.RayTracingScene.RayTracingSceneRHI, TEXT("TLAS does not exist. Verify that the current pass is represented in Lumen::AnyLumenHardwareRayTracingPassEnabled()."));
	SharedParameters->TLAS = View.RayTracingScene.RayTracingSceneRHI->GetShaderResourceView();

	// Lighting data
	SharedParameters->LightDataPacked = View.RayTracingLightData.UniformBuffer;
	SharedParameters->LightDataBuffer = View.RayTracingLightData.LightBufferSRV;
	SharedParameters->SSProfilesTexture = GraphBuilder.RegisterExternalTexture(View.RayTracingSubSurfaceProfileTexture);

	// Use surface cache, instead
	GetLumenCardTracingParameters(View, TracingInputs, SharedParameters->TracingParameters);
	SharedParameters->MeshSDFGridParameters = MeshSDFGridParameters;
}

#endif // RHI_RAYTRACING