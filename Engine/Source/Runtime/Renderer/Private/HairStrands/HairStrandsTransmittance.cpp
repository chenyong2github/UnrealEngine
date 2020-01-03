// Copyright Epic Games, Inc. All Rights Reserved.

#include "HairStrandsTransmittance.h"
#include "HairStrandsCluster.h"
#include "HairStrandsLUT.h"
#include "HairStrandsDeepShadow.h"
#include "HairStrandsVoxelization.h"
#include "HairStrandsRendering.h"

#include "Shader.h"
#include "GlobalShader.h"
#include "ShaderParameters.h"
#include "ShaderParameterStruct.h"
#include "SceneTextureParameters.h"
#include "RenderGraphUtils.h"
#include "PostProcessing.h"

///////////////////////////////////////////////////////////////////////////////////////////////////

static int32 GDeepShadowDebugMode = 0;
static FAutoConsoleVariableRef CVarDeepShadowDebugMode(TEXT("r.HairStrands.DeepShadow.DebugMode"), GDeepShadowDebugMode, TEXT("Color debug mode for deep shadow"));
static uint32 GetDeepShadowDebugMode() { return uint32(FMath::Max(0, GDeepShadowDebugMode)); }

static int32 GDeepShadowKernelType = 2; // 0:linear, 1:PCF_2x2, 2: PCF_6x4, 3:PCSS
static float GDeepShadowKernelAperture = 1;
static FAutoConsoleVariableRef CVarDeepShadowKernelType(TEXT("r.HairStrands.DeepShadow.KernelType"), GDeepShadowKernelType, TEXT("Set the type of kernel used for evaluating hair transmittance, 0:linear, 1:PCF_2x2, 2: PCF_6x4, 3:PCSS"));
static FAutoConsoleVariableRef CVarDeepShadowKernelAperture(TEXT("r.HairStrands.DeepShadow.KernelAperture"), GDeepShadowKernelAperture, TEXT("Set the aperture angle, in degree, used by the kernel for evaluating the hair transmittance when using PCSS kernel"));

static uint32 GetDeepShadowKernelType() { return uint32(FMath::Max(0, GDeepShadowKernelType)); }
static float GetDeepShadowKernelAperture() { return GDeepShadowKernelAperture; }

static int32 GStrandHairShadowMaskKernelType = 2;
static FAutoConsoleVariableRef GVarDeepShadowShadowMaskKernelType(TEXT("r.HairStrands.DeepShadow.ShadowMaskKernelType"), GStrandHairShadowMaskKernelType, TEXT("Set the kernel type for filtering shadow cast by hair on opaque geometry (0:2x2, 1:4x4, 2:Gaussian8, 3:Gaussian16). Default is 0"));

static float GDeepShadowDensityScale = 2;	// Default is arbitrary, based on Mike asset
static float GDeepShadowDepthBiasScale = 2;	// Default is arbitrary, based on content test
static FAutoConsoleVariableRef CVarDeepShadowDensityScale(TEXT("r.HairStrands.DeepShadow.DensityScale"), GDeepShadowDensityScale, TEXT("Set density scale for compensating the lack of hair fiber in an asset"));
static FAutoConsoleVariableRef CVarDeepShadowDepthBiasScale(TEXT("r.HairStrands.DeepShadow.DepthBiasScale"), GDeepShadowDepthBiasScale, TEXT("Set depth bias scale for transmittance computation"));

float GetDeepShadowDensityScale() { return FMath::Max(0.0f, GDeepShadowDensityScale); }
float GetDeepShadowDepthBiasScale() { return FMath::Max(0.0f, GDeepShadowDepthBiasScale); }
///////////////////////////////////////////////////////////////////////////////////////////////////

static bool HasDeepShadowData(const FLightSceneInfo* LightSceneInfo, const FHairStrandsDeepShadowDatas& InDatas)
{
	for (const FHairStrandsDeepShadowData& DomData : InDatas.Datas)
	{
		if (DomData.LightId == LightSceneInfo->Id)
			return true;
	}

	return false;
}


///////////////////////////////////////////////////////////////////////////////////////////////////
// Transmittance Mask from deep shadow

enum FHairTransmittanceType
{
	FHairTransmittanceType_DeepShadow,
	FHairTransmittanceType_Voxel,
	FHairTransmittanceTypeCount
};

class FDeepTransmittanceMaskCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDeepTransmittanceMaskCS);
	SHADER_USE_PARAMETER_STRUCT(FDeepTransmittanceMaskCS, FGlobalShader);

	class FTransmittanceType : SHADER_PERMUTATION_INT("PERMUTATION_TRANSMITTANCE_TYPE", FHairTransmittanceTypeCount);
	class FTransmittanceGroupSize : SHADER_PERMUTATION_INT("PERMUTATION_GROUP_SIZE", 2);
	using FPermutationDomain = TShaderPermutationDomain<FTransmittanceType, FTransmittanceGroupSize>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)

		SHADER_PARAMETER_ARRAY(FIntVector4, DeepShadow_AtlasSlotOffsets, [FHairStrandsDeepShadowData::MaxClusterCount])
		SHADER_PARAMETER_ARRAY(FMatrix, DeepShadow_WorldToLightTransforms, [FHairStrandsDeepShadowData::MaxClusterCount])
		SHADER_PARAMETER(FIntPoint, DeepShadow_Resolution)
		SHADER_PARAMETER(FVector, LightDirection)
		SHADER_PARAMETER(uint32, MaxVisibilityNodeCount)
		SHADER_PARAMETER(FVector4, LightPosition)
		SHADER_PARAMETER(float, DepthBiasScale)
		SHADER_PARAMETER(float, DensityScale)
		SHADER_PARAMETER(float, DeepShadow_KernelAperture)
		SHADER_PARAMETER(uint32, DeepShadow_KernelType)
		SHADER_PARAMETER(uint32, DeepShadow_DebugMode)
		SHADER_PARAMETER(FMatrix, DeepShadow_ShadowToWorld)

		SHADER_PARAMETER_ARRAY(FVector4, Voxel_MinAABBs, [FHairStrandsDeepShadowData::MaxClusterCount])
		SHADER_PARAMETER_ARRAY(FVector4, Voxel_MaxAABBs, [FHairStrandsDeepShadowData::MaxClusterCount])
		SHADER_PARAMETER(uint32, Voxel_Resolution)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, Voxel_DensityTexture0)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, Voxel_DensityTexture1)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, Voxel_DensityTexture2)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, Voxel_DensityTexture3)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, Voxel_DensityTexture4)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, Voxel_DensityTexture5)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, Voxel_DensityTexture6)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, Voxel_DensityTexture7)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DeepShadow_FrontDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DeepShadow_DomTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, HairLUTTexture)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, HairVisibilityNodeData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, HairVisibilityNodeCoord)
		SHADER_PARAMETER_RDG_BUFFER(StructuredBuffer, IndirectArgsBuffer)

		SHADER_PARAMETER_RDG_BUFFER_UAV(StructuredBuffer, OutputColor)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, ShadowSampler)

		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
};

IMPLEMENT_GLOBAL_SHADER(FDeepTransmittanceMaskCS, "/Engine/Private/HairStrands/HairStrandsDeepTransmittanceMask.usf", "MainCS", SF_Compute);

struct FDeepShadowTransmittanceParams
{
	FIntVector4 DeepShadow_AtlasSlotOffsets[FHairStrandsDeepShadowData::MaxClusterCount];
	FMatrix DeepShadow_WorldToLightTransforms[FHairStrandsDeepShadowData::MaxClusterCount];
	FIntPoint DeepShadow_Resolution = FIntPoint(0, 0);
	FVector LightDirection = FVector::ZeroVector;
	FVector4 LightPosition = FVector4(0, 0, 0, 0);
	float DepthBiasScale = 0;
	float DensityScale = 0;
	FMatrix DeepShadow_ShadowToWorld = FMatrix::Identity;

	FRDGTextureRef DeepShadow_FrontDepthTexture = nullptr;
	FRDGTextureRef DeepShadow_DomTexture = nullptr;

	FRDGBufferRef HairVisibilityNodeData = nullptr;
	FRDGBufferRef HairVisibilityNodeCoord = nullptr;

	FRDGTextureRef Cluster_DensityTextures[FHairStrandsDeepShadowData::MaxClusterCount];
	FVector4 Cluster_MinAABBs[FHairStrandsDeepShadowData::MaxClusterCount];
	FVector4 Cluster_MaxAABBs[FHairStrandsDeepShadowData::MaxClusterCount];
	uint32  Cluster_Resolution;
};

// Transmittance mask
static FRDGBufferRef AddDeepShadowTransmittanceMaskPass(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View,
	const FHairTransmittanceType TransmittanceType,
	const FDeepShadowTransmittanceParams& Params,
	const uint32 NodeGroupSize,
	FRDGTextureRef HairLUTTexture,
	FRDGBufferRef IndirectArgsBuffer)
{
	FRDGBufferRef OutBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(
		4 * sizeof(float),
		Params.HairVisibilityNodeData->Desc.NumElements),
		TEXT("HairTransmittanceNodeData"));

	FDeepTransmittanceMaskCS::FParameters* Parameters = GraphBuilder.AllocParameters<FDeepTransmittanceMaskCS::FParameters>();
	Parameters->ViewUniformBuffer = View.ViewUniformBuffer;
	Parameters->SceneTextures = SceneTextures;
	Parameters->HairLUTTexture = HairLUTTexture;
	Parameters->DeepShadow_FrontDepthTexture = Params.DeepShadow_FrontDepthTexture;
	Parameters->DeepShadow_DomTexture = Params.DeepShadow_DomTexture;
	Parameters->LinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters->OutputColor = GraphBuilder.CreateUAV(OutBuffer);
	Parameters->DeepShadow_Resolution = Params.DeepShadow_Resolution;
	Parameters->LightDirection = Params.LightDirection;
	Parameters->LightPosition = Params.LightPosition;
	Parameters->DepthBiasScale = Params.DepthBiasScale;
	Parameters->DensityScale = Params.DensityScale;
	Parameters->DeepShadow_KernelAperture = GetDeepShadowKernelAperture();
	Parameters->DeepShadow_KernelType = GetDeepShadowKernelType();
	Parameters->DeepShadow_DebugMode = GetDeepShadowDebugMode();
	Parameters->DeepShadow_ShadowToWorld = Params.DeepShadow_ShadowToWorld;
	Parameters->IndirectArgsBuffer = IndirectArgsBuffer;
	Parameters->MaxVisibilityNodeCount = Params.HairVisibilityNodeData->Desc.NumElements;

	memcpy(&(Parameters->DeepShadow_AtlasSlotOffsets[0]), Params.DeepShadow_AtlasSlotOffsets, sizeof(FIntVector4) * FHairStrandsDeepShadowData::MaxClusterCount);
	memcpy(&(Parameters->DeepShadow_WorldToLightTransforms[0]), Params.DeepShadow_WorldToLightTransforms, sizeof(FMatrix) * FHairStrandsDeepShadowData::MaxClusterCount);

	memcpy(&(Parameters->Voxel_MinAABBs[0]), Params.Cluster_MinAABBs, sizeof(FVector4) * FHairStrandsDeepShadowData::MaxClusterCount);
	memcpy(&(Parameters->Voxel_MaxAABBs[0]), Params.Cluster_MaxAABBs, sizeof(FVector4) * FHairStrandsDeepShadowData::MaxClusterCount);
	Parameters->Voxel_Resolution = Params.Cluster_Resolution;
	Parameters->Voxel_DensityTexture0 = Params.Cluster_DensityTextures[0];
	Parameters->Voxel_DensityTexture1 = Params.Cluster_DensityTextures[1];
	Parameters->Voxel_DensityTexture2 = Params.Cluster_DensityTextures[2];
	Parameters->Voxel_DensityTexture3 = Params.Cluster_DensityTextures[3];
	Parameters->Voxel_DensityTexture4 = Params.Cluster_DensityTextures[4];
	Parameters->Voxel_DensityTexture5 = Params.Cluster_DensityTextures[5];
	Parameters->Voxel_DensityTexture6 = Params.Cluster_DensityTextures[6];
	Parameters->Voxel_DensityTexture7 = Params.Cluster_DensityTextures[7];

	Parameters->HairVisibilityNodeData = GraphBuilder.CreateSRV(Params.HairVisibilityNodeData);
	Parameters->HairVisibilityNodeCoord = GraphBuilder.CreateSRV(Params.HairVisibilityNodeCoord);

	check(NodeGroupSize == 64 || NodeGroupSize == 32);
	FDeepTransmittanceMaskCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FDeepTransmittanceMaskCS::FTransmittanceType>(TransmittanceType);
	PermutationVector.Set<FDeepTransmittanceMaskCS::FTransmittanceGroupSize>(NodeGroupSize == 64 ? 0 : (NodeGroupSize == 32 ? 1 : 2));

	TShaderMapRef<FDeepTransmittanceMaskCS> ComputeShader(View.ShaderMap, PermutationVector);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsTransmittanceMask"),
		*ComputeShader,
		Parameters,
		IndirectArgsBuffer,
		0);

	return OutBuffer;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Opaque Mask from deep shadow

enum FHairOpaqueMaskType
{
	FHairOpaqueMaskType_DeepShadow,
	FHairOpaqueMaskType_Voxel,
	FHairOpaqueMaskTypeCount
};

class FDeepShadowMaskPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDeepShadowMaskPS);
	SHADER_USE_PARAMETER_STRUCT(FDeepShadowMaskPS, FGlobalShader);

	class FOpaqueMaskType : SHADER_PERMUTATION_INT("PERMUTATION_OPAQUEMASK_TYPE", FHairOpaqueMaskTypeCount);
	class FKernelType : SHADER_PERMUTATION_INT("PERMUTATION_KERNEL_TYPE", 4);
	using FPermutationDomain = TShaderPermutationDomain<FOpaqueMaskType, FKernelType>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)

		SHADER_PARAMETER(FIntPoint, DeepShadow_SlotOffset)
		SHADER_PARAMETER(FIntPoint, DeepShadow_SlotResolution)
		SHADER_PARAMETER(FMatrix, DeepShadow_WorldToLightTransform)
		SHADER_PARAMETER(uint32, DeepShadow_IsWholeSceneLight)

		SHADER_PARAMETER(FVector4, Voxel_LightPosition)
		SHADER_PARAMETER(FVector, Voxel_LightDirection)
		SHADER_PARAMETER(float, Voxel_DensityScale)
		SHADER_PARAMETER(FVector, Voxel_MinAABB)
		SHADER_PARAMETER(uint32, Voxel_Resolution)
		SHADER_PARAMETER(FVector, Voxel_MaxAABB)

		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, Voxel_DensityTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DeepShadow_FrontDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CategorizationTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, ShadowSampler)

		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		RENDER_TARGET_BINDING_SLOTS()
		END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
};

IMPLEMENT_GLOBAL_SHADER(FDeepShadowMaskPS, "/Engine/Private/HairStrands/HairStrandsDeepShadowMask.usf", "MainPS", SF_Pixel);


struct FDeepShadowOpaqueParams
{
	FRDGTextureRef	CategorizationTexture = nullptr;

	FMatrix			DeepShadow_WorldToLightTransform;
	FIntRect		DeepShadow_AtlasRect;
	FRDGTextureRef	DeepShadow_FrontDepthTexture = nullptr;
	bool			DeepShadow_bIsWholeSceneLight = false;

	FVector			Voxel_LightDirection = FVector::ZeroVector;
	FVector4		Voxel_LightPosition = FVector4(0, 0, 0, 0);
	float			Voxel_DensityScale = 0;
	FRDGTextureRef	Voxel_DensityTexture = nullptr;
	FVector			Voxel_MinAABB;
	FVector			Voxel_MaxAABB;
	uint32			Voxel_Resolution;
};

// Opaque mask
static void AddDeepShadowOpaqueMaskPass(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View,
	const FHairOpaqueMaskType HairOpaqueMaskType,
	const FDeepShadowOpaqueParams& Params,
	FRDGTextureRef& OutShadowMask)
{
	check(OutShadowMask);

	FDeepShadowMaskPS::FParameters* Parameters = GraphBuilder.AllocParameters<FDeepShadowMaskPS::FParameters>();
	Parameters->ViewUniformBuffer = View.ViewUniformBuffer;
	Parameters->SceneTextures = SceneTextures;
	Parameters->DeepShadow_WorldToLightTransform = Params.DeepShadow_WorldToLightTransform;
	Parameters->DeepShadow_FrontDepthTexture = Params.DeepShadow_FrontDepthTexture;
	Parameters->CategorizationTexture = Params.CategorizationTexture;
	Parameters->LinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters->ShadowSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters->DeepShadow_IsWholeSceneLight = Params.DeepShadow_bIsWholeSceneLight ? 1 : 0;
	Parameters->RenderTargets[0] = FRenderTargetBinding(OutShadowMask, ERenderTargetLoadAction::ELoad);
	Parameters->DeepShadow_SlotOffset = FIntPoint(Params.DeepShadow_AtlasRect.Min.X, Params.DeepShadow_AtlasRect.Min.Y);
	Parameters->DeepShadow_SlotResolution = FIntPoint(Params.DeepShadow_AtlasRect.Max.X - Params.DeepShadow_AtlasRect.Min.X, Params.DeepShadow_AtlasRect.Max.Y - Params.DeepShadow_AtlasRect.Min.Y);

	Parameters->Voxel_LightPosition = Params.Voxel_LightPosition;
	Parameters->Voxel_LightDirection = Params.Voxel_LightDirection;
	Parameters->Voxel_DensityScale = Params.Voxel_DensityScale;
	Parameters->Voxel_MinAABB = Params.Voxel_MinAABB;
	Parameters->Voxel_Resolution = Params.Voxel_Resolution;
	Parameters->Voxel_MaxAABB = Params.Voxel_MaxAABB;

	Parameters->Voxel_DensityTexture = Params.Voxel_DensityTexture;

	FDeepShadowMaskPS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FDeepShadowMaskPS::FOpaqueMaskType>(HairOpaqueMaskType);
	PermutationVector.Set<FDeepShadowMaskPS::FKernelType>(FMath::Clamp(GStrandHairShadowMaskKernelType, 0, 3));

	const FIntPoint OutputResolution = SceneTextures.SceneDepthBuffer->Desc.Extent;
	TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
	TShaderMapRef<FDeepShadowMaskPS> PixelShader(View.ShaderMap, PermutationVector);
	const TShaderMap<FGlobalShaderType>* GlobalShaderMap = View.ShaderMap;
	const FIntRect Viewport = View.ViewRect;
	const FViewInfo* CapturedView = &View;

	{
		ClearUnusedGraphResources(*PixelShader, Parameters);
		FIntPoint Resolution = OutShadowMask->Desc.Extent;

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("HairStrandsShadowMask"),
			Parameters,
			ERDGPassFlags::Raster,
			[Parameters, VertexShader, PixelShader, Viewport, Resolution, CapturedView](FRHICommandList& RHICmdList)
		{
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Min, BF_One, BF_One, BO_Min, BF_One, BF_One>::GetRHI(); // Min Operator
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			VertexShader->SetParameters(RHICmdList, CapturedView->ViewUniformBuffer);
			RHICmdList.SetViewport(Viewport.Min.X, Viewport.Min.Y, 0.0f, Viewport.Max.X, Viewport.Max.Y, 1.0f);
			SetShaderParameters(RHICmdList, *PixelShader, PixelShader->GetPixelShader(), *Parameters);

			DrawRectangle(
				RHICmdList,
				0, 0,
				Viewport.Width(), Viewport.Height(),
				Viewport.Min.X, Viewport.Min.Y,
				Viewport.Width(), Viewport.Height(),
				Viewport.Size(),
				Resolution,
				*VertexShader,
				EDRF_UseTriangleOptimization);
		});
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////

static FHairStrandsTransmittanceMaskData RenderHairStrandsTransmittanceMask(
	FRHICommandListImmediate& RHICmdList,
	const FViewInfo& View,
	const FLightSceneInfo* LightSceneInfo,
	const FHairStrandsDeepShadowDatas& DeepShadowDatas,
	const FHairStrandsClusterDatas& DeepClusterDatas,
	const FHairStrandsVisibilityData& VisibilityData)
{
	if (DeepClusterDatas.Datas.Num() == 0)
		return FHairStrandsTransmittanceMaskData();

	if (!HasDeepShadowData(LightSceneInfo, DeepShadowDatas) && !IsHairStrandsVoxelizationEnable())
		return FHairStrandsTransmittanceMaskData();

	DECLARE_GPU_STAT(HairStrandsTransmittanceMask);
	SCOPED_DRAW_EVENT(RHICmdList, HairStrandsTransmittanceMask);
	SCOPED_GPU_STAT(RHICmdList, HairStrandsTransmittanceMask);

	const FHairLUT InHairLUT = GetHairLUT(RHICmdList, View);

	FSceneRenderTargets& SceneTargets = FSceneRenderTargets::Get(RHICmdList);
	FRDGBuilder GraphBuilder(RHICmdList);

	// Note: GbufferB.a store the shading model on the 4 lower bits (MATERIAL_SHADINGMODEL_HAIR)
	FSceneTextureParameters SceneTextures;
	SetupSceneTextureParameters(GraphBuilder, &SceneTextures);

	FRDGTextureRef HairLUTTexture = GraphBuilder.RegisterExternalTexture(InHairLUT.Textures[HairLUTType_DualScattering], TEXT("HairLUTTexture"));
	FRDGBufferRef NodeIndirectArgBuffer = GraphBuilder.RegisterExternalBuffer(VisibilityData.NodeIndirectArg, TEXT("HairNodeIndirectArgBuffer"));

	FDeepShadowTransmittanceParams Params;
	Params.HairVisibilityNodeData = GraphBuilder.RegisterExternalBuffer(VisibilityData.NodeData, TEXT("HairVisibilityNodeData"));
	Params.HairVisibilityNodeCoord = GraphBuilder.RegisterExternalBuffer(VisibilityData.NodeCoord, TEXT("HairVisibilityNodeCoord"));
	Params.DensityScale = GetDeepShadowDensityScale();
	memset(Params.DeepShadow_AtlasSlotOffsets, 0, sizeof(Params.DeepShadow_AtlasSlotOffsets));
	memset(Params.DeepShadow_WorldToLightTransforms, 0, sizeof(Params.DeepShadow_WorldToLightTransforms));

	FRDGBufferRef OutShadowMask = nullptr;
	bool bHasFoundLight = false;
	if (!IsHairStrandsForVoxelTransmittanceAndShadowEnable())
	{
		for (const FHairStrandsDeepShadowData& DeepShadowData : DeepShadowDatas.Datas)
		{
			if (DeepShadowData.LightId == LightSceneInfo->Id)
			{
				bHasFoundLight = true;
				if (!Params.DeepShadow_FrontDepthTexture)
				{
					Params.DeepShadow_FrontDepthTexture = GraphBuilder.RegisterExternalTexture(DeepShadowData.DepthTexture, TEXT("DeepShadow_FrontDepthTexture"));
					Params.DeepShadow_DomTexture = GraphBuilder.RegisterExternalTexture(DeepShadowData.LayersTexture, TEXT("DeepShadow_DomTexture"));
				}
				Params.DeepShadow_Resolution = DeepShadowData.ShadowResolution;
				Params.LightDirection = DeepShadowData.LightDirection;
				Params.LightPosition = DeepShadowData.LightPosition;
				Params.DepthBiasScale = GetDeepShadowDepthBiasScale();
				Params.DeepShadow_AtlasSlotOffsets[DeepShadowData.ClusterId] = FIntVector4(DeepShadowData.AtlasRect.Min.X, DeepShadowData.AtlasRect.Min.Y, DeepShadowData.AtlasRect.Width(), DeepShadowData.AtlasRect.Height());
				Params.DeepShadow_WorldToLightTransforms[DeepShadowData.ClusterId] = DeepShadowData.WorldToLightTransform;
			}
		}

		if (bHasFoundLight)
		{
			check(Params.DeepShadow_FrontDepthTexture);
			check(Params.DeepShadow_DomTexture);
			OutShadowMask = AddDeepShadowTransmittanceMaskPass(
				GraphBuilder,
				SceneTextures,
				View,
				FHairTransmittanceType_DeepShadow,
				Params,
				VisibilityData.NodeGroupSize,
				HairLUTTexture,
				NodeIndirectArgBuffer);
		}
	}

	if (!bHasFoundLight && IsHairStrandsVoxelizationEnable())
	{
		Params.Cluster_Resolution = 0;
		memset(Params.Cluster_MinAABBs, 0, sizeof(FVector4) * FHairStrandsDeepShadowData::MaxClusterCount);
		memset(Params.Cluster_MaxAABBs, 0, sizeof(FVector4) * FHairStrandsDeepShadowData::MaxClusterCount);

		TRefCountPtr<IPooledRenderTarget> DummyVoxelResources;
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::CreateVolumeDesc(1, 1, 1, PF_R32_UINT, FClearValueBinding::Black, TexCreate_None, TexCreate_UAV | TexCreate_ShaderResource, false, 1));
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, DummyVoxelResources, TEXT("DummyDensityTexture"));
		FRDGTextureRef DefaultDensityTexture = GraphBuilder.RegisterExternalTexture(DummyVoxelResources, TEXT("Voxel_DefaultDensityTexture"));
		for (uint32 TexIt = 0; TexIt < FHairStrandsDeepShadowData::MaxClusterCount; ++TexIt)
		{
			Params.Cluster_DensityTextures[TexIt] = DefaultDensityTexture;
		}

		Params.DensityScale = GetHairStrandsVoxelizationDensityScale();
		Params.DepthBiasScale = GetHairStrandsVoxelizationDepthBiasScale();
		Params.LightDirection = LightSceneInfo->Proxy->GetDirection();
		Params.LightPosition = FVector4(LightSceneInfo->Proxy->GetPosition(), LightSceneInfo->Proxy->GetLightType() == ELightComponentType::LightType_Directional ? 0 : 1);
		for (const FHairStrandsClusterData& ClusterData : DeepClusterDatas.Datas)
		{
			Params.Cluster_Resolution = ClusterData.GetResolution();
			Params.Cluster_MinAABBs[ClusterData.ClusterId] = ClusterData.GetMinBound();
			Params.Cluster_MaxAABBs[ClusterData.ClusterId] = ClusterData.GetMaxBound();
			if (ClusterData.VoxelResources.DensityTexture)
				Params.Cluster_DensityTextures[ClusterData.ClusterId] = GraphBuilder.RegisterExternalTexture(ClusterData.VoxelResources.DensityTexture);
		}

		OutShadowMask = AddDeepShadowTransmittanceMaskPass(
			GraphBuilder,
			SceneTextures,
			View,
			FHairTransmittanceType_Voxel,
			Params,
			VisibilityData.NodeGroupSize,
			HairLUTTexture,
			NodeIndirectArgBuffer);
	}

	FHairStrandsTransmittanceMaskData OutTransmittanceMaskData;
	GraphBuilder.QueueBufferExtraction(OutShadowMask, &OutTransmittanceMaskData.TransmittanceMask, FRDGResourceState::EAccess::Read, FRDGResourceState::EPipeline::Graphics);

	// #RDG_todo/#Hair_todo
	// Keep an extra ref to keep the buffer alive until the .Execute() function. The issue arive by the fact the indirect buffer is never 
	// explicitly referenced in the graph. So its reference count is never incremented, and this makes it culled during the graph dependency 
	// walk.
	TRefCountPtr<FPooledRDGBuffer> DummyNodeIndirectArg;
	GraphBuilder.QueueBufferExtraction(NodeIndirectArgBuffer, &DummyNodeIndirectArg, FRDGResourceState::EAccess::Read, FRDGResourceState::EPipeline::Compute);

	GraphBuilder.Execute();
	OutTransmittanceMaskData.TransmittanceMaskSRV = RHICreateShaderResourceView(OutTransmittanceMaskData.TransmittanceMask->StructuredBuffer);

	return OutTransmittanceMaskData;
}

FHairStrandsTransmittanceMaskData RenderHairStrandsTransmittanceMask(
	FRHICommandListImmediate& RHICmdList,
	const TArray<FViewInfo>& Views,
	const FLightSceneInfo* LightSceneInfo,
	const FHairStrandsDatas* HairDatas)
{
	FHairStrandsTransmittanceMaskData TransmittanceMaskData;
	if (HairDatas)
	{
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			const FViewInfo& View = Views[ViewIndex];
			const FHairStrandsDeepShadowDatas& InDeepShadowDatas = HairDatas->DeepShadowViews.Views[ViewIndex];
			const FHairStrandsVisibilityData& InHairVisibilityData = HairDatas->HairVisibilityViews.HairDatas[ViewIndex];
			const FHairStrandsClusterDatas& InDeepClusterDatas = HairDatas->HairClusterPerViews.Views[ViewIndex];

			TransmittanceMaskData = RenderHairStrandsTransmittanceMask(RHICmdList, View, LightSceneInfo, InDeepShadowDatas, InDeepClusterDatas, InHairVisibilityData);
		}
	}
	return TransmittanceMaskData;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

static void RenderHairStrandsShadowMask(
	FRHICommandListImmediate& RHICmdList,
	const FViewInfo& View,
	const FLightSceneInfo* LightSceneInfo,
	IPooledRenderTarget* ScreenShadowMaskTexture,
	const FHairStrandsDeepShadowDatas& DeepShadowDatas,
	const FHairStrandsVisibilityData& InVisibilityData,
	const FHairStrandsClusterDatas& InClusterDatas)
{
	if (InClusterDatas.Datas.Num() == 0)
		return;

	if (!HasDeepShadowData(LightSceneInfo, DeepShadowDatas) && !IsHairStrandsVoxelizationEnable())
		return;

	DECLARE_GPU_STAT(HairStrandsOpaqueMask);
	SCOPED_DRAW_EVENT(RHICmdList, HairStrandsOpaqueMask);
	SCOPED_GPU_STAT(RHICmdList, HairStrandsOpaqueMask);

	FSceneRenderTargets& SceneTargets = FSceneRenderTargets::Get(RHICmdList);
	FRDGBuilder GraphBuilder(RHICmdList);

	// Note: GbufferB.a store the shading model on the 4 lower bits (MATERIAL_SHADINGMODEL_HAIR)
	FSceneTextureParameters SceneTextures;
	SetupSceneTextureParameters(GraphBuilder, &SceneTextures);

	FRDGTextureRef Categorization = RegisterExternalTextureWithFallback(GraphBuilder, InVisibilityData.CategorizationTexture, GSystemTextures.BlackDummy);
	FRDGTextureRef OutShadowMask = GraphBuilder.RegisterExternalTexture(ScreenShadowMaskTexture, TEXT("ScreenShadowMaskTexture"));

	bool bHasDeepShadow = false;
	if (!IsHairStrandsForVoxelTransmittanceAndShadowEnable())
	{
		for (const FHairStrandsDeepShadowData& DomData : DeepShadowDatas.Datas)
		{
			if (DomData.LightId != LightSceneInfo->Id)
				continue;

			bHasDeepShadow = true;
			FRDGTextureRef DeepShadowDepth = GraphBuilder.RegisterExternalTexture(DomData.DepthTexture, TEXT("DeepShadowDepthTexture"));
			const bool bIsWholeSceneLight = LightSceneInfo->Proxy->GetLightType() == LightType_Directional;

			FDeepShadowOpaqueParams Params;
			Params.CategorizationTexture = Categorization;
			Params.DeepShadow_WorldToLightTransform = DomData.WorldToLightTransform;
			Params.DeepShadow_AtlasRect = DomData.AtlasRect;
			Params.DeepShadow_FrontDepthTexture = DeepShadowDepth;
			Params.DeepShadow_bIsWholeSceneLight = bIsWholeSceneLight;

			AddDeepShadowOpaqueMaskPass(
				GraphBuilder,
				SceneTextures,
				View,
				FHairOpaqueMaskType_DeepShadow,
				Params,
				OutShadowMask);
		}
	}

	// Code is disabled for now until we have the full DOM/voxel fallback logic
	// If there is no deep shadow for this light, fallback on the voxel representation
	if (!bHasDeepShadow && IsHairStrandsVoxelizationEnable())
	{
		for (const FHairStrandsClusterData& ClusterData : InClusterDatas.Datas)
		{
			const bool bIsWholeSceneLight = LightSceneInfo->Proxy->GetLightType() == LightType_Directional;

			FDeepShadowOpaqueParams Params;
			Params.CategorizationTexture = Categorization;
			Params.Voxel_Resolution = ClusterData.GetResolution();
			Params.Voxel_MinAABB = ClusterData.GetMinBound();
			Params.Voxel_MaxAABB = ClusterData.GetMaxBound();
			Params.Voxel_DensityTexture = GraphBuilder.RegisterExternalTexture(ClusterData.VoxelResources.DensityTexture ? ClusterData.VoxelResources.DensityTexture : GSystemTextures.WhiteDummy, TEXT("Voxel_DensityTexture"));
			Params.Voxel_DensityScale = GetDeepShadowDensityScale();
			Params.Voxel_LightDirection = LightSceneInfo->Proxy->GetDirection();
			Params.Voxel_LightPosition = FVector4(LightSceneInfo->Proxy->GetPosition(), LightSceneInfo->Proxy->GetLightType() == ELightComponentType::LightType_Directional ? 0 : 1);

			AddDeepShadowOpaqueMaskPass(
				GraphBuilder,
				SceneTextures,
				View,
				FHairOpaqueMaskType_Voxel,
				Params,
				OutShadowMask);
		}
	}

	TRefCountPtr<IPooledRenderTarget> LocalOutput = GSystemTextures.BlackDummy;
	GraphBuilder.QueueTextureExtraction(OutShadowMask, &LocalOutput);

	GraphBuilder.Execute();
}

void RenderHairStrandsShadowMask(
	FRHICommandListImmediate& RHICmdList,
	const TArray<FViewInfo>& Views,
	const FLightSceneInfo* LightSceneInfo,
	IPooledRenderTarget* ScreenShadowMaskTexture,
	const FHairStrandsDatas* HairDatas)
{
	const FHairStrandsDeepShadowViews& DeepShadowViews = HairDatas->DeepShadowViews;
	const FHairStrandsVisibilityViews& HairVisibilityViews = HairDatas->HairVisibilityViews;
	const FHairStrandsClusterViews& HairClusterViews = HairDatas->HairClusterPerViews;

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		if (ViewIndex < DeepShadowViews.Views.Num() && ViewIndex < HairVisibilityViews.HairDatas.Num() && ViewIndex < HairClusterViews.Views.Num())
		{
			const FHairStrandsVisibilityData& HairVisibilityData = HairVisibilityViews.HairDatas[ViewIndex];
			const FHairStrandsDeepShadowDatas& DeepShadowDatas = DeepShadowViews.Views[ViewIndex];
			const FHairStrandsClusterDatas& ClusterDatas = HairClusterViews.Views[ViewIndex];
			RenderHairStrandsShadowMask(RHICmdList, Views[ViewIndex], LightSceneInfo, ScreenShadowMaskTexture, DeepShadowDatas, HairVisibilityData, ClusterDatas);
		}
	}
}
