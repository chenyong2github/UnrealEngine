// Copyright Epic Games, Inc. All Rights Reserved.

#include "PrimitiveUniformShaderParameters.h"
#include "InstanceUniformShaderParameters.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveSceneInfo.h"
#include "ProfilingDebugging/LoadTimeTracker.h"

void FSinglePrimitiveStructured::InitRHI() 
{
	SCOPED_LOADTIMER(FSinglePrimitiveStructuredBuffer_InitRHI);

	if (RHISupportsComputeShaders(GMaxRHIShaderPlatform))
	{
		FRHIResourceCreateInfo CreateInfo(TEXT("PrimitiveSceneDataBuffer"));

		{	
			PrimitiveSceneDataBufferRHI = RHICreateStructuredBuffer(sizeof(FVector4), FPrimitiveSceneShaderData::PrimitiveDataStrideInFloat4s * sizeof(FVector4), BUF_Static | BUF_ShaderResource, CreateInfo);
			PrimitiveSceneDataBufferSRV = RHICreateShaderResourceView(PrimitiveSceneDataBufferRHI);
		}

		{
			CreateInfo.DebugName = TEXT("PrimitiveSceneDataTexture");
			PrimitiveSceneDataTextureRHI = RHICreateTexture2D(FPrimitiveSceneShaderData::PrimitiveDataStrideInFloat4s, 1, PF_A32B32G32R32F, 1, 1, TexCreate_ShaderResource | TexCreate_UAV, CreateInfo);
			PrimitiveSceneDataTextureSRV = RHICreateShaderResourceView(PrimitiveSceneDataTextureRHI, 0);
		}

		CreateInfo.DebugName = TEXT("LightmapSceneDataBuffer");
		LightmapSceneDataBufferRHI = RHICreateStructuredBuffer(sizeof(FVector4), FLightmapSceneShaderData::LightmapDataStrideInFloat4s * sizeof(FVector4), BUF_Static | BUF_ShaderResource, CreateInfo);
		LightmapSceneDataBufferSRV = RHICreateShaderResourceView(LightmapSceneDataBufferRHI);

		CreateInfo.DebugName = TEXT("InstanceSceneDataBuffer");
		InstanceSceneDataBufferRHI = RHICreateStructuredBuffer(sizeof(FVector4), FInstanceSceneShaderData::InstanceDataStrideInFloat4s * sizeof(FVector4), BUF_Static | BUF_ShaderResource, CreateInfo);
		InstanceSceneDataBufferSRV = RHICreateShaderResourceView(InstanceSceneDataBufferRHI);

		CreateInfo.DebugName = TEXT("SkyIrradianceEnvironmentMap");
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

		LockedData = RHILockBuffer(PrimitiveSceneDataBufferRHI, 0, FPrimitiveSceneShaderData::PrimitiveDataStrideInFloat4s * sizeof(FVector4), RLM_WriteOnly);
		FPlatformMemory::Memcpy(LockedData, PrimitiveSceneData.Data.GetData(), FPrimitiveSceneShaderData::PrimitiveDataStrideInFloat4s * sizeof(FVector4));
		RHIUnlockBuffer(PrimitiveSceneDataBufferRHI);

		LockedData = RHILockBuffer(LightmapSceneDataBufferRHI, 0, FLightmapSceneShaderData::LightmapDataStrideInFloat4s * sizeof(FVector4), RLM_WriteOnly);
		FPlatformMemory::Memcpy(LockedData, LightmapSceneData.Data.GetData(), FLightmapSceneShaderData::LightmapDataStrideInFloat4s * sizeof(FVector4));
		RHIUnlockBuffer(LightmapSceneDataBufferRHI);

		LockedData = RHILockBuffer(InstanceSceneDataBufferRHI, 0, FInstanceSceneShaderData::InstanceDataStrideInFloat4s * sizeof(FVector4), RLM_WriteOnly);
		FPlatformMemory::Memcpy(LockedData, InstanceSceneData.Data.GetData(), FInstanceSceneShaderData::InstanceDataStrideInFloat4s * sizeof(FVector4));
		RHIUnlockBuffer(InstanceSceneDataBufferRHI);
	}

//#if WITH_EDITOR
	if (IsFeatureLevelSupported(GMaxRHIShaderPlatform, ERHIFeatureLevel::SM5))
	{
		// Create level instance SRV
		FRHIResourceCreateInfo LevelInstanceBufferCreateInfo(TEXT("EditorVisualizeLevelInstanceDataBuffer"));
		EditorVisualizeLevelInstanceDataBufferRHI = RHICreateVertexBuffer(sizeof(uint32), BUF_Static | BUF_ShaderResource, LevelInstanceBufferCreateInfo);

		void* LockedData = RHILockBuffer(EditorVisualizeLevelInstanceDataBufferRHI, 0, sizeof(uint32), RLM_WriteOnly);

		*reinterpret_cast<uint32*>(LockedData) = 0;

		RHIUnlockBuffer(EditorVisualizeLevelInstanceDataBufferRHI);

		EditorVisualizeLevelInstanceDataBufferSRV = RHICreateShaderResourceView(EditorVisualizeLevelInstanceDataBufferRHI, sizeof(uint32), PF_R32_UINT);

		// Create selection outline SRV
		FRHIResourceCreateInfo SelectionBufferCreateInfo(TEXT("EditorSelectedDataBuffer"));
		EditorSelectedDataBufferRHI = RHICreateVertexBuffer(sizeof(uint32), BUF_Static | BUF_ShaderResource, SelectionBufferCreateInfo);

		LockedData = RHILockBuffer(EditorSelectedDataBufferRHI, 0, sizeof(uint32), RLM_WriteOnly);

		*reinterpret_cast<uint32*>(LockedData) = 0;

		RHIUnlockBuffer(EditorSelectedDataBufferRHI);

		EditorSelectedDataBufferSRV = RHICreateShaderResourceView(EditorSelectedDataBufferRHI, sizeof(uint32), PF_R32_UINT);
	}
//#endif
}

TGlobalResource<FSinglePrimitiveStructured> GIdentityPrimitiveBuffer;
TGlobalResource<FSinglePrimitiveStructured> GTilePrimitiveBuffer;

FPrimitiveSceneShaderData::FPrimitiveSceneShaderData(const FPrimitiveSceneProxy* RESTRICT Proxy)
	: Data(InPlace, NoInit)
{
	bool bHasPrecomputedVolumetricLightmap;
	FMatrix PreviousLocalToWorld;
	int32 SingleCaptureIndex;
	bool bOutputVelocity;

	Proxy->GetScene().GetPrimitiveUniformShaderParameters_RenderThread(
		Proxy->GetPrimitiveSceneInfo(),
		bHasPrecomputedVolumetricLightmap,
		PreviousLocalToWorld,
		SingleCaptureIndex,
		bOutputVelocity
	);

	FBoxSphereBounds PreSkinnedLocalBounds;
	Proxy->GetPreSkinnedLocalBounds(PreSkinnedLocalBounds);

	FPrimitiveSceneInfo* PrimitiveSceneInfo = Proxy->GetPrimitiveSceneInfo();

	Setup(
		FPrimitiveUniformShaderParametersBuilder{}
		.Defaults()
			.LocalToWorld(Proxy->GetLocalToWorld())
			.PreviousLocalToWorld(PreviousLocalToWorld)
			.ActorWorldPosition(Proxy->GetActorPosition())
			.WorldBounds(Proxy->GetBounds())
			.LocalBounds(Proxy->GetLocalBounds())
			.PreSkinnedLocalBounds(PreSkinnedLocalBounds)
			.CustomPrimitiveData(Proxy->GetCustomPrimitiveData())
			.LightingChannelMask(Proxy->GetLightingChannelMask())
			.LightmapDataIndex(Proxy->GetPrimitiveSceneInfo()->GetLightmapDataOffset())
			.LightmapUVIndex(Proxy->GetLightMapCoordinateIndex())
			.SingleCaptureIndex(SingleCaptureIndex)
			.InstanceDataOffset(Proxy->GetPrimitiveSceneInfo()->GetInstanceDataOffset())
			.NumInstanceDataEntries(Proxy->GetPrimitiveSceneInfo()->GetNumInstanceDataEntries())
			.ReceivesDecals(Proxy->ReceivesDecals())
			.DrawsVelocity(Proxy->DrawsVelocity())
			.OutputVelocity(bOutputVelocity)
			.CastContactShadow(Proxy->CastsContactShadow())
			.CastShadow(Proxy->CastsDynamicShadow())
			.HasCapsuleRepresentation(Proxy->HasDynamicIndirectShadowCasterRepresentation())
			.UseVolumetricLightmap(bHasPrecomputedVolumetricLightmap)
		.Build()
	);
}

void FPrimitiveSceneShaderData::Setup(const FPrimitiveUniformShaderParameters& PrimitiveUniformShaderParameters)
{
	static_assert(sizeof(FPrimitiveUniformShaderParameters) == sizeof(FPrimitiveSceneShaderData), "The FPrimitiveSceneShaderData manual layout below and in usf must match FPrimitiveUniformShaderParameters.  Update this assert when adding a new member.");
	static_assert(NUM_LIGHTING_CHANNELS == 3, "The FPrimitiveSceneShaderData packing currently assumes a maximum of 3 lighting channels.");

	// Note: layout must match GetPrimitiveData in usf

	// Set W directly in order to bypass NaN check, when passing int through FVector to shader.

	Data[0].X	= *(const float*)&PrimitiveUniformShaderParameters.Flags;
	Data[0].Y	= *(const float*)&PrimitiveUniformShaderParameters.InstanceDataOffset;
	Data[0].Z	= *(const float*)&PrimitiveUniformShaderParameters.NumInstanceDataEntries;
	Data[0].W	= *(const float*)&PrimitiveUniformShaderParameters.SingleCaptureIndex;

	Data[1]		= *(const FVector4*)&PrimitiveUniformShaderParameters.LocalToWorld.M[0][0];
	Data[2]		= *(const FVector4*)&PrimitiveUniformShaderParameters.LocalToWorld.M[1][0];
	Data[3]		= *(const FVector4*)&PrimitiveUniformShaderParameters.LocalToWorld.M[2][0];
	Data[4]		= *(const FVector4*)&PrimitiveUniformShaderParameters.LocalToWorld.M[3][0];

	Data[5]		= *(const FVector4*)&PrimitiveUniformShaderParameters.WorldToLocal.M[0][0];
	Data[6]		= *(const FVector4*)&PrimitiveUniformShaderParameters.WorldToLocal.M[1][0];
	Data[7]		= *(const FVector4*)&PrimitiveUniformShaderParameters.WorldToLocal.M[2][0];
	Data[8]		= *(const FVector4*)&PrimitiveUniformShaderParameters.WorldToLocal.M[3][0];

	Data[9]		= *(const FVector4*)&PrimitiveUniformShaderParameters.PreviousLocalToWorld.M[0][0];
	Data[10]	= *(const FVector4*)&PrimitiveUniformShaderParameters.PreviousLocalToWorld.M[1][0];
	Data[11]	= *(const FVector4*)&PrimitiveUniformShaderParameters.PreviousLocalToWorld.M[2][0];
	Data[12]	= *(const FVector4*)&PrimitiveUniformShaderParameters.PreviousLocalToWorld.M[3][0];

	Data[13]	= *(const FVector4*)&PrimitiveUniformShaderParameters.PreviousWorldToLocal.M[0][0];
	Data[14]	= *(const FVector4*)&PrimitiveUniformShaderParameters.PreviousWorldToLocal.M[1][0];
	Data[15]	= *(const FVector4*)&PrimitiveUniformShaderParameters.PreviousWorldToLocal.M[2][0];
	Data[16]	= *(const FVector4*)&PrimitiveUniformShaderParameters.PreviousWorldToLocal.M[3][0];

	Data[17]	= FVector4(PrimitiveUniformShaderParameters.InvNonUniformScale, PrimitiveUniformShaderParameters.ObjectBoundsX);
	Data[18]	= PrimitiveUniformShaderParameters.ObjectWorldPositionAndRadius;

	Data[19]	= FVector4(PrimitiveUniformShaderParameters.ActorWorldPosition, 0.0f);
	Data[19].W	= *(const float*)&PrimitiveUniformShaderParameters.LightmapUVIndex;

	Data[20]	= FVector4(PrimitiveUniformShaderParameters.ObjectOrientation, 0.0f);
	Data[20].W	= *(const float*)&PrimitiveUniformShaderParameters.LightmapDataIndex;

	Data[21]	= PrimitiveUniformShaderParameters.NonUniformScale;

	Data[22]	= FVector4(PrimitiveUniformShaderParameters.PreSkinnedLocalBoundsMin, 0.0f);
	Data[22].W	= *(const float*)&PrimitiveUniformShaderParameters.NaniteResourceID;

	Data[23]	= FVector4(PrimitiveUniformShaderParameters.PreSkinnedLocalBoundsMax, 0.0f);
	Data[23].W	= *(const float*)&PrimitiveUniformShaderParameters.NaniteHierarchyOffset;

	Data[24]	= FVector4(PrimitiveUniformShaderParameters.LocalObjectBoundsMin, PrimitiveUniformShaderParameters.ObjectBoundsY);
	Data[25]	= FVector4(PrimitiveUniformShaderParameters.LocalObjectBoundsMax, PrimitiveUniformShaderParameters.ObjectBoundsZ);

	// Set all the custom primitive data float4. This matches the loop in SceneData.ush
	const int32 CustomPrimitiveDataStartIndex = 26;
	for (int32 DataIndex = 0; DataIndex < FCustomPrimitiveData::NumCustomPrimitiveDataFloat4s; ++DataIndex)
	{
		Data[CustomPrimitiveDataStartIndex + DataIndex] = PrimitiveUniformShaderParameters.CustomPrimitiveData[DataIndex];
	}
}
