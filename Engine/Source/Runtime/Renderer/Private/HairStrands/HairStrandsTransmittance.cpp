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
#include "GpuDebugRendering.h"
#include "ShaderPrintParameters.h"
#include "ShaderPrint.h"

///////////////////////////////////////////////////////////////////////////////////////////////////

static int32 GDeepShadowDebugMode = 0;
static FAutoConsoleVariableRef CVarDeepShadowDebugMode(TEXT("r.HairStrands.DeepShadow.DebugMode"), GDeepShadowDebugMode, TEXT("Color debug mode for deep shadow"));
static uint32 GetDeepShadowDebugMode() { return uint32(FMath::Max(0, GDeepShadowDebugMode)); }

static int32 GDeepShadowKernelType = 2; // 0:linear, 1:PCF_2x2, 2: PCF_6x4, 3:PCSS
static float GDeepShadowKernelAperture = 1;
static FAutoConsoleVariableRef CVarDeepShadowKernelType(TEXT("r.HairStrands.DeepShadow.KernelType"), GDeepShadowKernelType, TEXT("Set the type of kernel used for evaluating hair transmittance, 0:linear, 1:PCF_2x2, 2: PCF_6x4, 3:PCSS, 4:PCF_6x6_Accurate"));
static FAutoConsoleVariableRef CVarDeepShadowKernelAperture(TEXT("r.HairStrands.DeepShadow.KernelAperture"), GDeepShadowKernelAperture, TEXT("Set the aperture angle, in degree, used by the kernel for evaluating the hair transmittance when using PCSS kernel"));

static uint32 GetDeepShadowKernelType() { return uint32(FMath::Max(0, GDeepShadowKernelType)); }
static float GetDeepShadowKernelAperture() { return GDeepShadowKernelAperture; }

static int32 GStrandHairShadowMaskKernelType = 4;
static FAutoConsoleVariableRef GVarDeepShadowShadowMaskKernelType(TEXT("r.HairStrands.DeepShadow.ShadowMaskKernelType"), GStrandHairShadowMaskKernelType, TEXT("Set the kernel type for filtering shadow cast by hair on opaque geometry (0:2x2, 1:4x4, 2:Gaussian8, 3:Gaussian16, 4:Gaussian8 with transmittance. Default is 4"));

static float GDeepShadowDensityScale = 2;	// Default is arbitrary, based on Mike asset
static float GDeepShadowDepthBiasScale = 0.05;
static FAutoConsoleVariableRef CVarDeepShadowDensityScale(TEXT("r.HairStrands.DeepShadow.DensityScale"), GDeepShadowDensityScale, TEXT("Set density scale for compensating the lack of hair fiber in an asset"));
static FAutoConsoleVariableRef CVarDeepShadowDepthBiasScale(TEXT("r.HairStrands.DeepShadow.DepthBiasScale"), GDeepShadowDepthBiasScale, TEXT("Set depth bias scale for transmittance computation"));

static int32 GHairStrandsTransmittanceSuperSampling = 0;
static FAutoConsoleVariableRef CVarHairStrandsTransmittanceSuperSampling(TEXT("r.HairStrands.DeepShadow.SuperSampling"), GHairStrandsTransmittanceSuperSampling, TEXT("Evaluate transmittance with supersampling. This is expensive and intended to be used only in cine mode."), ECVF_Scalability | ECVF_RenderThreadSafe);

static int32 GHairStrandsTransmittanceMaskUseMipTraversal = 1;
static FAutoConsoleVariableRef CVarHairStrandsTransmittanceMaskUseMipTraversal(TEXT("r.HairStrands.DeepShadow.MipTraversal"), GHairStrandsTransmittanceMaskUseMipTraversal, TEXT("Evaluate transmittance using mip-map traversal (faster)."), ECVF_Scalability | ECVF_RenderThreadSafe);

static int32 GHairStrandsShadowRandomTraversalType = 2;
static FAutoConsoleVariableRef CVarHairStrandsShadowRandomTraversalType(TEXT("r.HairStrands.DeepShadow.RandomType"), GHairStrandsShadowRandomTraversalType, TEXT("Change how traversal jittering is initialized. Valid value are 0, 1, and 2. Each type makes different type of tradeoff."), ECVF_Scalability | ECVF_RenderThreadSafe);

static float GetDeepShadowDensityScale() { return FMath::Max(0.0f, GDeepShadowDensityScale); }
static float GetDeepShadowDepthBiasScale() { return FMath::Max(0.0f, GDeepShadowDepthBiasScale); }
///////////////////////////////////////////////////////////////////////////////////////////////////

static bool HasDeepShadowData(const FLightSceneInfo* LightSceneInfo, const FHairStrandsMacroGroupDatas& InDatas)
{
	for (const FHairStrandsMacroGroupData& MacroGroupData : InDatas.Datas)
	{
		for (const FHairStrandsDeepShadowData& DomData : MacroGroupData.DeepShadowDatas.Datas)
		{
			if (DomData.LightId == LightSceneInfo->Id)
				return true;
		}
	}

	return false;
}

FVector4 ComputeDeepShadowLayerDepths(float LayerDistribution)
{
	// LayerDistribution in [0..1]
	// Exponent in [1 .. 6.2]
	// Default LayerDistribution is 0.5, which is mapped onto exponent=3.1, making the last layer at depth 0.5f in clip space
	// Within this range the last layer's depth goes from 1 to 0.25 in clip space (prior to inverse Z)
	const float Exponent = FMath::Clamp(LayerDistribution, 0.f, 1.f) * 5.2f + 1;
	FVector4 Depths;
	Depths.X = FMath::Pow(0.2f, Exponent);
	Depths.Y = FMath::Pow(0.4f, Exponent);
	Depths.Z = FMath::Pow(0.6f, Exponent);
	Depths.W = FMath::Pow(0.8f, Exponent);
	return Depths;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Clear transmittance Mask

class FHairStrandsClearTransmittanceMaskCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairStrandsClearTransmittanceMaskCS);
	SHADER_USE_PARAMETER_STRUCT(FHairStrandsClearTransmittanceMaskCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, ElementCount)
		SHADER_PARAMETER_RDG_BUFFER_UAV(StructuredBuffer, OutputMask)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_CLEAR"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairStrandsClearTransmittanceMaskCS, "/Engine/Private/HairStrands/HairStrandsDeepTransmittanceMask.usf", "MainCS", SF_Compute);

static void AddHairStrandsClearTransmittanceMaskPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	FRDGBufferRef OutTransmittanceMask)
{
	FHairStrandsClearTransmittanceMaskCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairStrandsClearTransmittanceMaskCS::FParameters>();
	Parameters->ElementCount = OutTransmittanceMask->Desc.NumElements;
	Parameters->OutputMask = GraphBuilder.CreateUAV(OutTransmittanceMask);

	FHairStrandsClearTransmittanceMaskCS::FPermutationDomain PermutationVector;
	TShaderMapRef<FHairStrandsClearTransmittanceMaskCS> ComputeShader(ShaderMap, PermutationVector);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsClearTransmittanceMask"),
		ComputeShader,
		Parameters,
		FComputeShaderUtils::GetGroupCount(Parameters->ElementCount, 64));
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Transmittance buffer

static FRDGBufferRef CreateHairStrandsTransmittanceMaskBuffer(FRDGBuilder& GraphBuilder, uint32 NumElements)
{
	return GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(
		4 * sizeof(float),
		NumElements),
		TEXT("HairTransmittanceNodeData"));
}

FHairStrandsTransmittanceMaskData CreateDummyHairStrandsTransmittanceMaskData(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap)
{
	FHairStrandsTransmittanceMaskData Out;
	Out.TransmittanceMask = CreateHairStrandsTransmittanceMaskBuffer(GraphBuilder, 1);
	Out.TransmittanceMaskSRV = GraphBuilder.CreateSRV(Out.TransmittanceMask);
	AddHairStrandsClearTransmittanceMaskPass(GraphBuilder, ShaderMap, Out.TransmittanceMask);
	return Out;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Transmittance Mask from deep shadow

enum FHairTransmittanceType
{
	FHairTransmittanceType_DeepShadow,
	FHairTransmittanceType_VirtualVoxel,
	FHairTransmittanceTypeCount
};

class FDeepTransmittanceMaskCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDeepTransmittanceMaskCS);
	SHADER_USE_PARAMETER_STRUCT(FDeepTransmittanceMaskCS, FGlobalShader);

	class FTransmittanceType : SHADER_PERMUTATION_INT("PERMUTATION_TRANSMITTANCE_TYPE", FHairTransmittanceTypeCount);
	class FTransmittanceGroupSize : SHADER_PERMUTATION_INT("PERMUTATION_GROUP_SIZE", 2);
	class FSuperSampling : SHADER_PERMUTATION_INT("PERMUTATION_SUPERSAMPLING", 2);
	class FTraversal : SHADER_PERMUTATION_INT("PERMUTATION_TRAVERSAL", 2);
	using FPermutationDomain = TShaderPermutationDomain<FTransmittanceType, FTransmittanceGroupSize, FSuperSampling, FTraversal>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderDrawDebug::FShaderDrawDebugParameters, ShaderDrawParameters)

		SHADER_PARAMETER_ARRAY(FIntVector4, DeepShadow_AtlasSlotOffsets_AtlasSlotIndex, [FHairStrandsDeepShadowData::MaxMacroGroupCount])
		SHADER_PARAMETER_ARRAY(FMatrix, DeepShadow_CPUWorldToLightTransforms, [FHairStrandsDeepShadowData::MaxMacroGroupCount])
		SHADER_PARAMETER(FIntPoint, DeepShadow_Resolution)
		SHADER_PARAMETER(float, LightRadius)
		SHADER_PARAMETER(FVector, LightDirection)
		SHADER_PARAMETER(uint32, MaxVisibilityNodeCount)
		SHADER_PARAMETER(FVector4, LightPosition)
		SHADER_PARAMETER(uint32, LightChannelMask)
		SHADER_PARAMETER(FVector4, DeepShadow_LayerDepths)
		SHADER_PARAMETER(float, DeepShadow_DepthBiasScale)
		SHADER_PARAMETER(float, DeepShadow_DensityScale)
		SHADER_PARAMETER(float, DeepShadow_KernelAperture)
		SHADER_PARAMETER(uint32, DeepShadow_KernelType)
		SHADER_PARAMETER(uint32, DeepShadow_DebugMode)
		SHADER_PARAMETER(FMatrix, DeepShadow_ShadowToWorld)
		SHADER_PARAMETER(uint32, DeepShadow_bIsGPUDriven)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, RayMarchMaskTexture)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DeepShadow_FrontDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DeepShadow_DomTexture)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, DeepShadow_WorldToLightTransformBuffer)

		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, HairLUTTexture)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, HairVisibilityNodeData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, HairVisibilityNodeCoord)
		SHADER_PARAMETER_RDG_BUFFER(StructuredBuffer, IndirectArgsBuffer)

		SHADER_PARAMETER_RDG_BUFFER_UAV(StructuredBuffer, OutputColor)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, ShadowSampler)

		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualVoxelParameters, VirtualVoxel)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) 
	{ 
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FTransmittanceType>() == FHairTransmittanceType_DeepShadow && 
			(PermutationVector.Get<FSuperSampling>() == 1 || PermutationVector.Get<FTraversal>() == 1))
		{
			return false;
		}

		return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform);
	}

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		if (PermutationVector.Get<FTransmittanceType>() == FHairTransmittanceType_DeepShadow)
		{
			PermutationVector.Set<FSuperSampling>(0);
			PermutationVector.Set<FTraversal>(0);
		}
		return PermutationVector;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_TRANSMITTANCE"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FDeepTransmittanceMaskCS, "/Engine/Private/HairStrands/HairStrandsDeepTransmittanceMask.usf", "MainCS", SF_Compute);

struct FDeepShadowTransmittanceParams
{
	FIntVector4 DeepShadow_AtlasSlotOffsets_AtlasSlotIndex[FHairStrandsDeepShadowData::MaxMacroGroupCount];
	FMatrix DeepShadow_CPUWorldToLightTransforms[FHairStrandsDeepShadowData::MaxMacroGroupCount];
	FRDGBufferSRVRef DeepShadow_WorldToLightTransformBuffer = nullptr;
	FIntPoint DeepShadow_Resolution = FIntPoint(0, 0);
	bool DeepShadow_bIsGPUDriven = false;
	FVector4 DeepShadow_LayerDepths = FVector4(0, 0, 0, 0);
	FVector LightDirection = FVector::ZeroVector;
	FVector4 LightPosition = FVector4(0, 0, 0, 0);
	uint32 LightChannelMask = 0;
	float LightRadius = 0;
	float DeepShadow_DepthBiasScale = 0;
	float DeepShadow_DensityScale = 0;
	FMatrix DeepShadow_ShadowToWorld = FMatrix::Identity;
	
	FRDGTextureRef DeepShadow_FrontDepthTexture = nullptr;
	FRDGTextureRef DeepShadow_DomTexture = nullptr;

	FRDGBufferRef HairVisibilityNodeData = nullptr;
	FRDGBufferRef HairVisibilityNodeCoord = nullptr;

	const FVirtualVoxelResources* VirtualVoxelResources = nullptr;
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
	FRDGBufferRef IndirectArgsBuffer,
	FRDGTextureRef ScreenShadowMaskSubPixelTexture)
{
	FRDGBufferRef OutBuffer = CreateHairStrandsTransmittanceMaskBuffer(GraphBuilder, Params.HairVisibilityNodeData->Desc.NumElements);

	FDeepTransmittanceMaskCS::FParameters* Parameters = GraphBuilder.AllocParameters<FDeepTransmittanceMaskCS::FParameters>();
	Parameters->ViewUniformBuffer = View.ViewUniformBuffer;
	Parameters->SceneTextures = SceneTextures;
	Parameters->HairLUTTexture = HairLUTTexture;
	Parameters->DeepShadow_FrontDepthTexture = Params.DeepShadow_FrontDepthTexture;
	Parameters->DeepShadow_DomTexture = Params.DeepShadow_DomTexture;
	Parameters->LinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters->OutputColor = GraphBuilder.CreateUAV(OutBuffer);
	Parameters->LightChannelMask = Params.LightChannelMask;
	Parameters->DeepShadow_Resolution = Params.DeepShadow_Resolution;
	Parameters->LightDirection = Params.LightDirection;
	Parameters->LightPosition = Params.LightPosition;
	Parameters->LightRadius = Params.LightRadius;
	Parameters->DeepShadow_DepthBiasScale = Params.DeepShadow_DepthBiasScale;
	Parameters->DeepShadow_DensityScale = Params.DeepShadow_DensityScale;
	Parameters->DeepShadow_KernelAperture = GetDeepShadowKernelAperture();
	Parameters->DeepShadow_KernelType = GetDeepShadowKernelType();
	Parameters->DeepShadow_DebugMode = GetDeepShadowDebugMode();
	Parameters->DeepShadow_LayerDepths = Params.DeepShadow_LayerDepths;
	Parameters->DeepShadow_ShadowToWorld = Params.DeepShadow_ShadowToWorld;
	Parameters->IndirectArgsBuffer = IndirectArgsBuffer;
	Parameters->MaxVisibilityNodeCount = Params.HairVisibilityNodeData->Desc.NumElements;
	Parameters->DeepShadow_bIsGPUDriven = Params.DeepShadow_bIsGPUDriven ? 1 : 0;;
	Parameters->DeepShadow_WorldToLightTransformBuffer = Params.DeepShadow_WorldToLightTransformBuffer;

	if (ShaderDrawDebug::IsShaderDrawDebugEnabled(View))
	{
		ShaderDrawDebug::SetParameters(GraphBuilder, View.ShaderDrawData, Parameters->ShaderDrawParameters);
	}

	memcpy(&(Parameters->DeepShadow_AtlasSlotOffsets_AtlasSlotIndex[0]), Params.DeepShadow_AtlasSlotOffsets_AtlasSlotIndex, sizeof(FIntVector4) * FHairStrandsDeepShadowData::MaxMacroGroupCount);
	memcpy(&(Parameters->DeepShadow_CPUWorldToLightTransforms[0]), Params.DeepShadow_CPUWorldToLightTransforms, sizeof(FMatrix) * FHairStrandsDeepShadowData::MaxMacroGroupCount);

	Parameters->RayMarchMaskTexture = ScreenShadowMaskSubPixelTexture ? ScreenShadowMaskSubPixelTexture : GraphBuilder.RegisterExternalTexture(GSystemTextures.WhiteDummy);

	bool bIsSuperSampled = false;
	if (TransmittanceType == FHairTransmittanceType_VirtualVoxel)
	{
		check(Params.VirtualVoxelResources);
		Parameters->VirtualVoxel = Params.VirtualVoxelResources->UniformBuffer;
		bIsSuperSampled = GHairStrandsTransmittanceSuperSampling > 0;
	}

	Parameters->HairVisibilityNodeData = GraphBuilder.CreateSRV(Params.HairVisibilityNodeData);
	Parameters->HairVisibilityNodeCoord = GraphBuilder.CreateSRV(Params.HairVisibilityNodeCoord);

	const bool bIsMipTraversal = GHairStrandsTransmittanceMaskUseMipTraversal > 0;
	check(NodeGroupSize == 64 || NodeGroupSize == 32);
	FDeepTransmittanceMaskCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FDeepTransmittanceMaskCS::FTransmittanceType>(TransmittanceType);
	PermutationVector.Set<FDeepTransmittanceMaskCS::FTransmittanceGroupSize>(NodeGroupSize == 64 ? 0 : (NodeGroupSize == 32 ? 1 : 2));
	PermutationVector.Set<FDeepTransmittanceMaskCS::FSuperSampling>(bIsSuperSampled ? 1 : 0);
	PermutationVector.Set<FDeepTransmittanceMaskCS::FTraversal>(bIsMipTraversal ? 1 : 0);
	PermutationVector = FDeepTransmittanceMaskCS::RemapPermutation(PermutationVector);

	TShaderMapRef<FDeepTransmittanceMaskCS> ComputeShader(View.ShaderMap, PermutationVector);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsTransmittanceMask"),
		ComputeShader,
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
	FHairOpaqueMaskType_VirtualVoxel,
	FHairOpaqueMaskTypeCount
};

class FDeepShadowMaskPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDeepShadowMaskPS);
	SHADER_USE_PARAMETER_STRUCT(FDeepShadowMaskPS, FGlobalShader);

	class FOpaqueMaskType : SHADER_PERMUTATION_INT("PERMUTATION_OPAQUEMASK_TYPE", FHairOpaqueMaskTypeCount);
	class FKernelType : SHADER_PERMUTATION_INT("PERMUTATION_KERNEL_TYPE", 5);
	using FPermutationDomain = TShaderPermutationDomain<FOpaqueMaskType, FKernelType>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderDrawDebug::FShaderDrawDebugParameters, ShaderDrawParameters)
		                         
		SHADER_PARAMETER(FIntPoint, DeepShadow_SlotOffset)
		SHADER_PARAMETER(uint32, DeepShadow_SlotIndex)
		SHADER_PARAMETER(FIntPoint, DeepShadow_SlotResolution)
		SHADER_PARAMETER(FMatrix, DeepShadow_CPUWorldToLightTransform)
		SHADER_PARAMETER(uint32, bIsWholeSceneLight)
		SHADER_PARAMETER(float, DeepShadow_DepthBiasScale)
		SHADER_PARAMETER(float, DeepShadow_DensityScale)
		SHADER_PARAMETER(uint32, DeepShadow_bIsGPUDriven)
		SHADER_PARAMETER(FVector4, DeepShadow_LayerDepths)

		SHADER_PARAMETER(FVector4, Voxel_LightPosition)
		SHADER_PARAMETER(FVector, Voxel_LightDirection)
		SHADER_PARAMETER(uint32, Voxel_MacroGroupId)
		SHADER_PARAMETER(uint32, Voxel_RandomType)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, RayMarchMaskTexture)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, DeepShadow_WorldToLightTransformBuffer)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DeepShadow_FrontDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DeepShadow_DomTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CategorizationTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, ShadowSampler)

		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualVoxelParameters, VirtualVoxel)
		RENDER_TARGET_BINDING_SLOTS()
		END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetRenderTargetOutputFormat(0, PF_B8G8R8A8);
	}
};

IMPLEMENT_GLOBAL_SHADER(FDeepShadowMaskPS, "/Engine/Private/HairStrands/HairStrandsDeepShadowMask.usf", "MainPS", SF_Pixel);


struct FDeepShadowOpaqueParams
{
	FRDGTextureRef	CategorizationTexture = nullptr;
	bool			bIsWholeSceneLight = false;

	FRDGBufferSRVRef DeepShadow_WorldToLightTransformBuffer = nullptr;
	FMatrix			DeepShadow_CPUWorldToLightTransform;
	FIntRect		DeepShadow_AtlasRect;
	FRDGTextureRef	DeepShadow_FrontDepthTexture = nullptr;
	FRDGTextureRef	DeepShadow_LayerTexture = nullptr;
	bool			DeepShadow_bIsGPUDriven = false;
	float			DeepShadow_DepthBiasScale = 1;
	float			DeepShadow_DensityScale = 1;
	uint32			DeepShadow_AtlasSlotIndex = 0;
	FVector4		DeepShadow_LayerDepths = FVector4(0, 0, 0, 0);

	FVector			Voxel_LightDirection = FVector::ZeroVector;
	FVector4		Voxel_LightPosition = FVector4(0, 0, 0, 0);
	uint32			Voxel_MacroGroupId;
	const FVirtualVoxelResources* Voxel_VirtualVoxel = nullptr;
};

// Opaque mask
static void AddDeepShadowOpaqueMaskPass(
	FRDGBuilder& GraphBuilder,
	const FRDGTextureRef& SceneDepthTexture,
	const FViewInfo& View,
	const FHairOpaqueMaskType HairOpaqueMaskType,
	const FDeepShadowOpaqueParams& Params,
	FRDGTextureRef& OutShadowMask)
{
	check(OutShadowMask);

	FDeepShadowMaskPS::FParameters* Parameters = GraphBuilder.AllocParameters<FDeepShadowMaskPS::FParameters>();
	Parameters->ViewUniformBuffer = View.ViewUniformBuffer;
	Parameters->SceneDepthTexture = SceneDepthTexture;
	Parameters->DeepShadow_CPUWorldToLightTransform = Params.DeepShadow_CPUWorldToLightTransform;
	Parameters->DeepShadow_FrontDepthTexture = Params.DeepShadow_FrontDepthTexture;
	Parameters->DeepShadow_DomTexture = Params.DeepShadow_LayerTexture;
	Parameters->CategorizationTexture = Params.CategorizationTexture;
	Parameters->LinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters->ShadowSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters->bIsWholeSceneLight = Params.bIsWholeSceneLight ? 1 : 0;
	Parameters->DeepShadow_DepthBiasScale = Params.DeepShadow_DepthBiasScale;
	Parameters->DeepShadow_DensityScale = Params.DeepShadow_DensityScale;
	Parameters->DeepShadow_LayerDepths = Params.DeepShadow_LayerDepths;
	Parameters->RenderTargets[0] = FRenderTargetBinding(OutShadowMask, ERenderTargetLoadAction::ELoad);
	Parameters->DeepShadow_LayerDepths = Params.DeepShadow_LayerDepths;
	Parameters->DeepShadow_SlotIndex = Params.DeepShadow_AtlasSlotIndex;
	Parameters->DeepShadow_SlotOffset = FIntPoint(Params.DeepShadow_AtlasRect.Min.X, Params.DeepShadow_AtlasRect.Min.Y);
	Parameters->DeepShadow_SlotResolution = FIntPoint(Params.DeepShadow_AtlasRect.Max.X - Params.DeepShadow_AtlasRect.Min.X, Params.DeepShadow_AtlasRect.Max.Y - Params.DeepShadow_AtlasRect.Min.Y);
	Parameters->DeepShadow_WorldToLightTransformBuffer = Params.DeepShadow_WorldToLightTransformBuffer;
	Parameters->DeepShadow_bIsGPUDriven = Params.DeepShadow_bIsGPUDriven ? 1 : 0;;

	if (ShaderDrawDebug::IsShaderDrawDebugEnabled(View))
	{
		ShaderDrawDebug::SetParameters(GraphBuilder, View.ShaderDrawData, Parameters->ShaderDrawParameters);
	}

	FRDGTextureRef RayMarchMask = nullptr;
	if (HairOpaqueMaskType == FHairOpaqueMaskType_VirtualVoxel)
	{
		FRDGTextureDesc Desc = OutShadowMask->Desc;
		Desc.Flags |= TexCreate_ShaderResource;
		RayMarchMask = GraphBuilder.CreateTexture(Desc, TEXT("RayMarchMask"));
		FRHICopyTextureInfo CopyInfo;
		CopyInfo.Size = OutShadowMask->Desc.GetSize();
		AddCopyTexturePass(GraphBuilder, OutShadowMask, RayMarchMask, CopyInfo);
	}
	Parameters->RayMarchMaskTexture = RayMarchMask;

	if (HairOpaqueMaskType == FHairOpaqueMaskType_VirtualVoxel)
	{
		check(Params.Voxel_VirtualVoxel);
		Parameters->Voxel_LightPosition = Params.Voxel_LightPosition;
		Parameters->Voxel_LightDirection = Params.Voxel_LightDirection;
		Parameters->Voxel_MacroGroupId = Params.Voxel_MacroGroupId;
		Parameters->VirtualVoxel = Params.Voxel_VirtualVoxel->UniformBuffer;
		Parameters->Voxel_RandomType = FMath::Clamp(GHairStrandsShadowRandomTraversalType, 0, 2);
	}

	FDeepShadowMaskPS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FDeepShadowMaskPS::FOpaqueMaskType>(HairOpaqueMaskType);
	PermutationVector.Set<FDeepShadowMaskPS::FKernelType>(FMath::Clamp(GStrandHairShadowMaskKernelType, 0, 4));

	const FIntPoint OutputResolution = SceneDepthTexture->Desc.Extent;
	TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
	TShaderMapRef<FDeepShadowMaskPS> PixelShader(View.ShaderMap, PermutationVector);
	const FGlobalShaderMap* GlobalShaderMap = View.ShaderMap;
	const FIntRect Viewport = View.ViewRect;
	const FViewInfo* CapturedView = &View;

	{
		ClearUnusedGraphResources(PixelShader, Parameters);
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
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			VertexShader->SetParameters(RHICmdList, CapturedView->ViewUniformBuffer);
			RHICmdList.SetViewport(Viewport.Min.X, Viewport.Min.Y, 0.0f, Viewport.Max.X, Viewport.Max.Y, 1.0f);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *Parameters);

			DrawRectangle(
				RHICmdList,
				0, 0,
				Viewport.Width(), Viewport.Height(),
				Viewport.Min.X, Viewport.Min.Y,
				Viewport.Width(), Viewport.Height(),
				Viewport.Size(),
				Resolution,
				VertexShader,
				EDRF_UseTriangleOptimization);
		});
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////

static FHairStrandsTransmittanceMaskData RenderHairStrandsTransmittanceMask(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FLightSceneInfo* LightSceneInfo,
	const FHairStrandsMacroGroupDatas& MacroGroupDatas,
	const FHairStrandsVisibilityData& VisibilityData,
	FRDGTextureRef ScreenShadowMaskSubPixelTexture)
{
	if (MacroGroupDatas.Datas.Num() == 0)
		return FHairStrandsTransmittanceMaskData();

	if (!HasDeepShadowData(LightSceneInfo, MacroGroupDatas) && !IsHairStrandsVoxelizationEnable())
		return FHairStrandsTransmittanceMaskData();

	DECLARE_GPU_STAT(HairStrandsTransmittanceMask);
	RDG_EVENT_SCOPE(GraphBuilder, "HairStrandsTransmittanceMask");
	RDG_GPU_STAT_SCOPE(GraphBuilder, HairStrandsTransmittanceMask);

	const FHairLUT InHairLUT = GetHairLUT(GraphBuilder, View);

	// Note: GbufferB.a store the shading model on the 4 lower bits (MATERIAL_SHADINGMODEL_HAIR)
	FSceneTextureParameters SceneTextures = GetSceneTextureParameters(GraphBuilder);

	FRDGTextureRef HairLUTTexture = InHairLUT.Textures[HairLUTType_DualScattering];

	FDeepShadowTransmittanceParams Params;
	Params.HairVisibilityNodeData = VisibilityData.NodeData;
	Params.HairVisibilityNodeCoord = VisibilityData.NodeCoord;
	Params.DeepShadow_DensityScale = GetDeepShadowDensityScale();
	Params.DeepShadow_DepthBiasScale = GetDeepShadowDepthBiasScale();
	memset(Params.DeepShadow_AtlasSlotOffsets_AtlasSlotIndex, 0, sizeof(Params.DeepShadow_AtlasSlotOffsets_AtlasSlotIndex));
	memset(Params.DeepShadow_CPUWorldToLightTransforms, 0, sizeof(Params.DeepShadow_CPUWorldToLightTransforms));


	FRDGBufferRef OutShadowMask = nullptr;
	bool bHasFoundLight = false;
	if (!IsHairStrandsForVoxelTransmittanceAndShadowEnable())
	{
		FRDGBufferSRVRef DeepShadow_WorldToLightTransformBufferSRV = nullptr;
		for (const FHairStrandsMacroGroupData& MacroGroupData : MacroGroupDatas.Datas)
		{
			for (const FHairStrandsDeepShadowData& DeepShadowData : MacroGroupData.DeepShadowDatas.Datas)
			{
				if (DeepShadowData.LightId == LightSceneInfo->Id)
				{
					if (DeepShadow_WorldToLightTransformBufferSRV == nullptr)
					{
						DeepShadow_WorldToLightTransformBufferSRV = GraphBuilder.CreateSRV(MacroGroupDatas.DeepShadowResources.DeepShadowWorldToLightTransforms);
					}

					bHasFoundLight = true;
					Params.DeepShadow_FrontDepthTexture = MacroGroupDatas.DeepShadowResources.DepthAtlasTexture;
					Params.DeepShadow_DomTexture = MacroGroupDatas.DeepShadowResources.LayersAtlasTexture;
					Params.DeepShadow_Resolution = DeepShadowData.ShadowResolution;
					Params.LightDirection = DeepShadowData.LightDirection;
					Params.LightPosition = DeepShadowData.LightPosition;
					Params.LightRadius = 0;
					Params.LightChannelMask = LightSceneInfo->Proxy->GetLightingChannelMask();
					Params.DeepShadow_LayerDepths = ComputeDeepShadowLayerDepths(DeepShadowData.LayerDistribution);
					Params.DeepShadow_AtlasSlotOffsets_AtlasSlotIndex[DeepShadowData.MacroGroupId] = FIntVector4(DeepShadowData.AtlasRect.Min.X, DeepShadowData.AtlasRect.Min.Y, DeepShadowData.AtlasSlotIndex, 0);
					Params.DeepShadow_CPUWorldToLightTransforms[DeepShadowData.MacroGroupId] = DeepShadowData.CPU_WorldToLightTransform;
					Params.DeepShadow_WorldToLightTransformBuffer = DeepShadow_WorldToLightTransformBufferSRV;
					Params.DeepShadow_bIsGPUDriven = MacroGroupDatas.DeepShadowResources.bIsGPUDriven;
				}
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
				VisibilityData.NodeIndirectArg,
				ScreenShadowMaskSubPixelTexture);
		}
	}

	if (!bHasFoundLight && MacroGroupDatas.VirtualVoxelResources.IsValid())
	{
		FLightShaderParameters LightParameters;
		LightSceneInfo->Proxy->GetLightShaderParameters(LightParameters);

		Params.DeepShadow_DensityScale = 0;
		Params.DeepShadow_DepthBiasScale = 0;
		Params.LightDirection = LightSceneInfo->Proxy->GetDirection();
		Params.LightPosition = FVector4(LightSceneInfo->Proxy->GetPosition(), LightSceneInfo->Proxy->GetLightType() == ELightComponentType::LightType_Directional ? 0 : 1);
		Params.LightChannelMask = LightSceneInfo->Proxy->GetLightingChannelMask();
		Params.LightRadius = FMath::Max(LightParameters.SourceLength, LightParameters.SourceRadius);
		Params.VirtualVoxelResources = &MacroGroupDatas.VirtualVoxelResources;

		OutShadowMask = AddDeepShadowTransmittanceMaskPass(
			GraphBuilder,
			SceneTextures,
			View,
			FHairTransmittanceType_VirtualVoxel,
			Params,
			VisibilityData.NodeGroupSize,
			HairLUTTexture,
			VisibilityData.NodeIndirectArg,
			ScreenShadowMaskSubPixelTexture);
	}

	FHairStrandsTransmittanceMaskData OutTransmittanceMaskData;
	OutTransmittanceMaskData.TransmittanceMask = OutShadowMask;
	OutTransmittanceMaskData.TransmittanceMaskSRV = GraphBuilder.CreateSRV(OutShadowMask);
	return OutTransmittanceMaskData;
}

FHairStrandsTransmittanceMaskData RenderHairStrandsTransmittanceMask(
	FRDGBuilder& GraphBuilder,
	const TArray<FViewInfo>& Views,
	const FLightSceneInfo* LightSceneInfo,
	const FHairStrandsRenderingData* HairDatas,
	FRDGTextureRef ScreenShadowMaskSubPixelTexture)
{
	FHairStrandsTransmittanceMaskData TransmittanceMaskData;
	if (HairDatas)
	{
		//for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		const int32 HairViewIndex = 0; // HAIR_TODO multiview support
		{
			const FViewInfo& View = Views[HairViewIndex];
			const FHairStrandsVisibilityData& InHairVisibilityData = HairDatas->HairVisibilityViews.HairDatas[HairViewIndex];
			const FHairStrandsMacroGroupDatas& InMacroGroupDatas = HairDatas->MacroGroupsPerViews.Views[HairViewIndex];

			TransmittanceMaskData = RenderHairStrandsTransmittanceMask(GraphBuilder, View, LightSceneInfo, InMacroGroupDatas, InHairVisibilityData, ScreenShadowMaskSubPixelTexture);
		}
	}
	return TransmittanceMaskData;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

static void RenderHairStrandsShadowMask(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FLightSceneInfo* LightSceneInfo,
	const FHairStrandsVisibilityData& InVisibilityData,
	const FHairStrandsMacroGroupDatas& InMacroGroupDatas,
	FRDGTextureRef OutShadowMask)
{
	if (InMacroGroupDatas.Datas.Num() == 0)
		return;

	if (!HasDeepShadowData(LightSceneInfo, InMacroGroupDatas) && !IsHairStrandsVoxelizationEnable())
		return;

	DECLARE_GPU_STAT(HairStrandsOpaqueMask);
	SCOPED_DRAW_EVENT(GraphBuilder.RHICmdList, HairStrandsOpaqueMask);
	SCOPED_GPU_STAT(GraphBuilder.RHICmdList, HairStrandsOpaqueMask);

	FSceneRenderTargets& SceneTargets = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);

	FRDGTextureRef SceneDepthTexture = GraphBuilder.RegisterExternalTexture(SceneTargets.SceneDepthZ, TEXT("SceneDephtTexture"));

	bool bHasDeepShadow = false;
	if (!IsHairStrandsForVoxelTransmittanceAndShadowEnable())
	{
		FRDGBufferRef DeepShadow_WorldToLightTransformBuffer = nullptr;
		FRDGBufferSRVRef DeepShadow_WorldToLightTransformBufferSRV = nullptr;

		for (const FHairStrandsMacroGroupData& MacroGroupData : InMacroGroupDatas.Datas)
		{
			for (const FHairStrandsDeepShadowData& DomData : MacroGroupData.DeepShadowDatas.Datas)
			{
				if (DomData.LightId != LightSceneInfo->Id)
					continue;

				if (DeepShadow_WorldToLightTransformBuffer == nullptr)
				{
					DeepShadow_WorldToLightTransformBuffer = InMacroGroupDatas.DeepShadowResources.DeepShadowWorldToLightTransforms;
					DeepShadow_WorldToLightTransformBufferSRV = GraphBuilder.CreateSRV(DeepShadow_WorldToLightTransformBuffer);
				}

				bHasDeepShadow = true;
				const bool bIsWholeSceneLight = LightSceneInfo->Proxy->GetLightType() == LightType_Directional;

				FDeepShadowOpaqueParams Params;
				Params.CategorizationTexture = InVisibilityData.CategorizationTexture;
				Params.DeepShadow_AtlasSlotIndex = DomData.AtlasSlotIndex;
				Params.DeepShadow_WorldToLightTransformBuffer = DeepShadow_WorldToLightTransformBufferSRV;
				Params.DeepShadow_bIsGPUDriven = InMacroGroupDatas.DeepShadowResources.bIsGPUDriven ? 1 : 0;
				Params.DeepShadow_CPUWorldToLightTransform = DomData.CPU_WorldToLightTransform;
				Params.DeepShadow_AtlasRect = DomData.AtlasRect;
				Params.DeepShadow_FrontDepthTexture = InMacroGroupDatas.DeepShadowResources.DepthAtlasTexture;
				Params.DeepShadow_LayerTexture = InMacroGroupDatas.DeepShadowResources.LayersAtlasTexture;
				Params.bIsWholeSceneLight = bIsWholeSceneLight;
				Params.DeepShadow_DepthBiasScale = GetDeepShadowDepthBiasScale();
				Params.DeepShadow_DensityScale = GetDeepShadowDensityScale();
				Params.DeepShadow_LayerDepths = ComputeDeepShadowLayerDepths(DomData.LayerDistribution);
				AddDeepShadowOpaqueMaskPass(
					GraphBuilder,
					SceneDepthTexture,
					View,
					FHairOpaqueMaskType_DeepShadow,
					Params,
					OutShadowMask);
			}
		}
	}

	// Code is disabled for now until we have the full DOM/voxel fallback logic
	// If there is no deep shadow for this light, fallback on the voxel representation
	if (!bHasDeepShadow && InMacroGroupDatas.VirtualVoxelResources.IsValid())
	{
		// TODO: Change this to be a single pass with virtual voxel?
		for (const FHairStrandsMacroGroupData& MacroGroupData : InMacroGroupDatas.Datas)
		{
			const bool bIsWholeSceneLight = LightSceneInfo->Proxy->GetLightType() == LightType_Directional;

			FDeepShadowOpaqueParams Params;
			Params.CategorizationTexture = InVisibilityData.CategorizationTexture;
			Params.Voxel_LightDirection = LightSceneInfo->Proxy->GetDirection();
			Params.Voxel_LightPosition = FVector4(LightSceneInfo->Proxy->GetPosition(), LightSceneInfo->Proxy->GetLightType() == ELightComponentType::LightType_Directional ? 0 : 1);
			Params.Voxel_MacroGroupId = MacroGroupData.MacroGroupId;
			Params.bIsWholeSceneLight = bIsWholeSceneLight ? 1 : 0;

			Params.Voxel_VirtualVoxel = &InMacroGroupDatas.VirtualVoxelResources;
			AddDeepShadowOpaqueMaskPass(
				GraphBuilder,
				SceneDepthTexture,
				View,
				FHairOpaqueMaskType_VirtualVoxel,
				Params,
				OutShadowMask);
		}
	}
}

void RenderHairStrandsShadowMask(
	FRDGBuilder& GraphBuilder,
	const TArray<FViewInfo>& Views,
	const FLightSceneInfo* LightSceneInfo,
	const FHairStrandsRenderingData* HairDatas,
	FRDGTextureRef OutShadowMask)
{
	if (Views.Num() == 0 || HairDatas == nullptr || OutShadowMask == nullptr)
	{
		return;
	}

	const FHairStrandsVisibilityViews& HairVisibilityViews = HairDatas->HairVisibilityViews;
	const FHairStrandsMacroGroupViews& MacroGroupViews = HairDatas->MacroGroupsPerViews;

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		if (ViewIndex < HairVisibilityViews.HairDatas.Num() && ViewIndex < MacroGroupViews.Views.Num())
		{
			const FHairStrandsVisibilityData& HairVisibilityData = HairVisibilityViews.HairDatas[ViewIndex];
			const FHairStrandsMacroGroupDatas& MacroGroupDatas = MacroGroupViews.Views[ViewIndex];
			if (HairVisibilityData.CategorizationTexture)
			{
				RenderHairStrandsShadowMask(GraphBuilder, Views[ViewIndex], LightSceneInfo, HairVisibilityData, MacroGroupDatas, OutShadowMask);
			}
		}
	}
}