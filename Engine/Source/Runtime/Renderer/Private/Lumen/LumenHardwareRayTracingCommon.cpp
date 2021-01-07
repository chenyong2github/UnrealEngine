// Copyright Epic Games, Inc. All Rights Reserved.

#if RHI_RAYTRACING

#include "LumenHardwareRayTracingCommon.h"

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