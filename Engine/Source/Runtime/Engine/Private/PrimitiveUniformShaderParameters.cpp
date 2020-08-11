// Copyright Epic Games, Inc. All Rights Reserved.

#include "PrimitiveUniformShaderParameters.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveSceneInfo.h"
#include "ProfilingDebugging/LoadTimeTracker.h"

void FSinglePrimitiveStructured::InitRHI() 
{
	SCOPED_LOADTIMER(FSinglePrimitiveStructuredBuffer_InitRHI);

	if (RHISupportsComputeShaders(GMaxRHIShaderPlatform))
	{
		FRHIResourceCreateInfo CreateInfo;

		{	
			PrimitiveSceneDataBufferRHI = RHICreateStructuredBuffer(sizeof(FVector4), FPrimitiveSceneShaderData::PrimitiveDataStrideInFloat4s * sizeof(FVector4), BUF_Static | BUF_ShaderResource, CreateInfo);
			PrimitiveSceneDataBufferSRV = RHICreateShaderResourceView(PrimitiveSceneDataBufferRHI);
		}

		{
			PrimitiveSceneDataTextureRHI = RHICreateTexture2D(FPrimitiveSceneShaderData::PrimitiveDataStrideInFloat4s, 1, PF_A32B32G32R32F, 1, 1, TexCreate_ShaderResource | TexCreate_UAV, CreateInfo);
			PrimitiveSceneDataTextureSRV = RHICreateShaderResourceView(PrimitiveSceneDataTextureRHI, 0);
		}

		LightmapSceneDataBufferRHI = RHICreateStructuredBuffer(sizeof(FVector4), FLightmapSceneShaderData::LightmapDataStrideInFloat4s * sizeof(FVector4), BUF_Static | BUF_ShaderResource, CreateInfo);
		LightmapSceneDataBufferSRV = RHICreateShaderResourceView(LightmapSceneDataBufferRHI);

		SkyIrradianceEnvironmentMapRHI = RHICreateStructuredBuffer(sizeof(FVector4), sizeof(FVector4) * 8, BUF_Static | BUF_ShaderResource, CreateInfo);
		SkyIrradianceEnvironmentMapSRV = RHICreateShaderResourceView(SkyIrradianceEnvironmentMapRHI);
	}

	UploadToGPU();
}

void FSinglePrimitiveStructured::UploadToGPU()
{
	if (RHISupportsComputeShaders(GMaxRHIShaderPlatform))
	{
		void* LockedData = nullptr;

		EShaderPlatform SafeShaderPlatform = ShaderPlatform < SP_NumPlatforms ? ShaderPlatform : GMaxRHIShaderPlatform;
		if (!GPUSceneUseTexture2D(SafeShaderPlatform))
		{
			LockedData = RHILockStructuredBuffer(PrimitiveSceneDataBufferRHI, 0, FPrimitiveSceneShaderData::PrimitiveDataStrideInFloat4s * sizeof(FVector4), RLM_WriteOnly);
			FPlatformMemory::Memcpy(LockedData, PrimitiveSceneData.Data, FPrimitiveSceneShaderData::PrimitiveDataStrideInFloat4s * sizeof(FVector4));
			RHIUnlockStructuredBuffer(PrimitiveSceneDataBufferRHI);
		}
		else
		{
			uint32 SrcStride;
			LockedData = RHILockTexture2D(PrimitiveSceneDataTextureRHI, 0, RLM_WriteOnly, SrcStride, false);
			FPlatformMemory::Memcpy(LockedData, PrimitiveSceneData.Data, FPrimitiveSceneShaderData::PrimitiveDataStrideInFloat4s * sizeof(FVector4));
			RHIUnlockTexture2D(PrimitiveSceneDataTextureRHI, 0, false);
		}

		LockedData = RHILockStructuredBuffer(LightmapSceneDataBufferRHI, 0, FLightmapSceneShaderData::LightmapDataStrideInFloat4s * sizeof(FVector4), RLM_WriteOnly);
		FPlatformMemory::Memcpy(LockedData, LightmapSceneData.Data, FLightmapSceneShaderData::LightmapDataStrideInFloat4s * sizeof(FVector4));
		RHIUnlockStructuredBuffer(LightmapSceneDataBufferRHI);
	}
}

TGlobalResource<FSinglePrimitiveStructured> GIdentityPrimitiveBuffer;
TGlobalResource<FSinglePrimitiveStructured> GTilePrimitiveBuffer;

FPrimitiveSceneShaderData::FPrimitiveSceneShaderData(const FPrimitiveSceneProxy* RESTRICT Proxy)
{
	bool bHasPrecomputedVolumetricLightmap;
	FMatrix PreviousLocalToWorld;
	int32 SingleCaptureIndex;
	bool bOutputVelocity;

	Proxy->GetScene().GetPrimitiveUniformShaderParameters_RenderThread(Proxy->GetPrimitiveSceneInfo(), bHasPrecomputedVolumetricLightmap, PreviousLocalToWorld, SingleCaptureIndex, bOutputVelocity);

	FBoxSphereBounds PreSkinnedLocalBounds;
	Proxy->GetPreSkinnedLocalBounds(PreSkinnedLocalBounds);

	Setup(GetPrimitiveUniformShaderParameters(
		Proxy->GetLocalToWorld(),
		PreviousLocalToWorld,
		Proxy->GetActorPosition(), 
		Proxy->GetBounds(), 
		Proxy->GetLocalBounds(),
		PreSkinnedLocalBounds,
		Proxy->ReceivesDecals(), 
		Proxy->HasDistanceFieldRepresentation(), 
		Proxy->HasDynamicIndirectShadowCasterRepresentation(), 
		Proxy->UseSingleSampleShadowFromStationaryLights(),
		bHasPrecomputedVolumetricLightmap,
		Proxy->DrawsVelocity(), 
		Proxy->GetLightingChannelMask(),
		Proxy->GetLpvBiasMultiplier(),
		Proxy->GetPrimitiveSceneInfo()->GetLightmapDataOffset(),
		SingleCaptureIndex,
        bOutputVelocity,
		Proxy->GetCustomPrimitiveData(),
		Proxy->CastsContactShadow()));
}

void FPrimitiveSceneShaderData::Setup(const FPrimitiveUniformShaderParameters& PrimitiveUniformShaderParameters)
{
	static_assert(sizeof(FPrimitiveUniformShaderParameters) == sizeof(FPrimitiveSceneShaderData), "The FPrimitiveSceneShaderData manual layout below and in usf must match FPrimitiveUniformShaderParameters.  Update this assert when adding a new member.");
	
	// Note: layout must match GetPrimitiveData in usf
	Data[0] = *(const FVector4*)&PrimitiveUniformShaderParameters.LocalToWorld.M[0][0];
	Data[1] = *(const FVector4*)&PrimitiveUniformShaderParameters.LocalToWorld.M[1][0];
	Data[2] = *(const FVector4*)&PrimitiveUniformShaderParameters.LocalToWorld.M[2][0];
	Data[3] = *(const FVector4*)&PrimitiveUniformShaderParameters.LocalToWorld.M[3][0];

	Data[4] = PrimitiveUniformShaderParameters.InvNonUniformScaleAndDeterminantSign;
	Data[5] = PrimitiveUniformShaderParameters.ObjectWorldPositionAndRadius;

	Data[6] = *(const FVector4*)&PrimitiveUniformShaderParameters.WorldToLocal.M[0][0];
	Data[7] = *(const FVector4*)&PrimitiveUniformShaderParameters.WorldToLocal.M[1][0];
	Data[8] = *(const FVector4*)&PrimitiveUniformShaderParameters.WorldToLocal.M[2][0];
	Data[9] = *(const FVector4*)&PrimitiveUniformShaderParameters.WorldToLocal.M[3][0];
	Data[10] = *(const FVector4*)&PrimitiveUniformShaderParameters.PreviousLocalToWorld.M[0][0];
	Data[11] = *(const FVector4*)&PrimitiveUniformShaderParameters.PreviousLocalToWorld.M[1][0];
	Data[12] = *(const FVector4*)&PrimitiveUniformShaderParameters.PreviousLocalToWorld.M[2][0];
	Data[13] = *(const FVector4*)&PrimitiveUniformShaderParameters.PreviousLocalToWorld.M[3][0];
	Data[14] = *(const FVector4*)&PrimitiveUniformShaderParameters.PreviousWorldToLocal.M[0][0];
	Data[15] = *(const FVector4*)&PrimitiveUniformShaderParameters.PreviousWorldToLocal.M[1][0];
	Data[16] = *(const FVector4*)&PrimitiveUniformShaderParameters.PreviousWorldToLocal.M[2][0];
	Data[17] = *(const FVector4*)&PrimitiveUniformShaderParameters.PreviousWorldToLocal.M[3][0];

	Data[18] = FVector4(PrimitiveUniformShaderParameters.ActorWorldPosition, PrimitiveUniformShaderParameters.UseSingleSampleShadowFromStationaryLights);
	Data[19] = FVector4(PrimitiveUniformShaderParameters.ObjectBounds, PrimitiveUniformShaderParameters.LpvBiasMultiplier);

	Data[20] = FVector4(
		PrimitiveUniformShaderParameters.DecalReceiverMask, 
		PrimitiveUniformShaderParameters.PerObjectGBufferData, 
		PrimitiveUniformShaderParameters.UseVolumetricLightmapShadowFromStationaryLights, 
		PrimitiveUniformShaderParameters.DrawsVelocity);
	Data[21] = PrimitiveUniformShaderParameters.ObjectOrientation;
	Data[22] = PrimitiveUniformShaderParameters.NonUniformScale;

	// Set W directly in order to bypass NaN check, when passing int through FVector to shader.
	Data[23] = FVector4(PrimitiveUniformShaderParameters.LocalObjectBoundsMin, 0.0f);
	Data[23].W = *(const float*)&PrimitiveUniformShaderParameters.LightingChannelMask;

	Data[24] = FVector4(PrimitiveUniformShaderParameters.LocalObjectBoundsMax, 0.0f);
	Data[24].W = *(const float*)&PrimitiveUniformShaderParameters.LightmapDataIndex;

	Data[25] = FVector4(PrimitiveUniformShaderParameters.PreSkinnedLocalBoundsMin, 0.0f);
	Data[25].W = *(const float*)&PrimitiveUniformShaderParameters.SingleCaptureIndex;

	Data[26] = FVector4(PrimitiveUniformShaderParameters.PreSkinnedLocalBoundsMax, 0.0f);
	Data[26].W = *(const float*)&PrimitiveUniformShaderParameters.OutputVelocity;

	// Set all the custom primitive data float4. This matches the loop in SceneData.ush
	const int32 CustomPrimitiveDataStartIndex = 27;
	for (int i = 0; i < FCustomPrimitiveData::NumCustomPrimitiveDataFloat4s; i++)
	{
		Data[CustomPrimitiveDataStartIndex + i] = PrimitiveUniformShaderParameters.CustomPrimitiveData[i];
	}
}

uint16 FPrimitiveSceneShaderData::GetPrimitivesPerTextureLine()
{
	// @todo texture size limit over 65536, revisit this in the future :). Currently you can have(with primitiveData = 35 floats4) a max of 122,683,392 primitives
	uint16 PrimitivesPerTextureLine = FMath::Min((int32)MAX_uint16, (int32)GMaxTextureDimensions) / (FPrimitiveSceneShaderData::PrimitiveDataStrideInFloat4s);
	return PrimitivesPerTextureLine;
}