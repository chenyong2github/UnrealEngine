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
#include "LightSceneInfo.h"
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

enum class EHairTransmittancePassType : uint8
{
	PerLight,
	OnePass
};

static bool HasDeepShadowData(const FLightSceneInfo* LightSceneInfo, const FHairStrandsMacroGroupDatas& InDatas)
{
	for (const FHairStrandsMacroGroupData& MacroGroupData : InDatas)
	{
		for (const FHairStrandsDeepShadowData& DomData : MacroGroupData.DeepShadowDatas)
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

struct FHairStrandsTransmittanceLightParams
{
	FVector LightDirection = FVector::ZeroVector;
	FVector4 LightPosition = FVector4(0, 0, 0, 0);
	uint32 LightChannelMask = 0;
	float LightRadius = 0;
};

///////////////////////////////////////////////////////////////////////////////////////////////////
// Clear transmittance Mask

class FHairStrandsClearTransmittanceMaskCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairStrandsClearTransmittanceMaskCS);
	SHADER_USE_PARAMETER_STRUCT(FHairStrandsClearTransmittanceMaskCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, ElementCount)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, OutputMask)
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
	Parameters->OutputMask = GraphBuilder.CreateUAV(OutTransmittanceMask, FHairStrandsTransmittanceMaskData::Format);

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
	return GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(
		sizeof(uint32),
		NumElements),
		TEXT("Hair.TransmittanceNodeData"));
}

FHairStrandsTransmittanceMaskData CreateDummyHairStrandsTransmittanceMaskData(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap)
{
	FHairStrandsTransmittanceMaskData Out;
	Out.TransmittanceMask = CreateHairStrandsTransmittanceMaskBuffer(GraphBuilder, 1);
	AddHairStrandsClearTransmittanceMaskPass(GraphBuilder, ShaderMap, Out.TransmittanceMask);
	return Out;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Transmittance Mask from voxel

class FHairStrandsVoxelTransmittanceMaskCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairStrandsVoxelTransmittanceMaskCS);
	SHADER_USE_PARAMETER_STRUCT(FHairStrandsVoxelTransmittanceMaskCS, FGlobalShader);

	class FTransmittanceGroupSize : SHADER_PERMUTATION_SPARSE_INT("PERMUTATION_GROUP_SIZE", 32, 64);
	class FSuperSampling : SHADER_PERMUTATION_INT("PERMUTATION_SUPERSAMPLING", 2);
	class FTraversal : SHADER_PERMUTATION_INT("PERMUTATION_TRAVERSAL", 2);
	class FOnePass : SHADER_PERMUTATION_BOOL("PERMUTATION_ONE_PASS");
	using FPermutationDomain = TShaderPermutationDomain<FTransmittanceGroupSize, FSuperSampling, FTraversal, FOnePass>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderDrawDebug::FShaderDrawDebugParameters, ShaderDrawParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters, ShaderPrintUniformBuffer)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FHairStrandsViewUniformParameters, HairStrands)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualVoxelParameters, VirtualVoxel)

		SHADER_PARAMETER(float, LightRadius)
		SHADER_PARAMETER(FVector3f, LightDirection)
		SHADER_PARAMETER(FVector4, LightPosition)
		SHADER_PARAMETER(uint32, LightChannelMask)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, ShadowMaskBitsTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, RayMarchMaskTexture)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, OutTransmittanceMask)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_STRUCT_REF(FForwardLightData, ForwardLightData)

		RDG_BUFFER_ACCESS(IndirectArgsBuffer, ERHIAccess::IndirectArgs)

	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		FForwardLightingParameters::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_TRANSMITTANCE_VOXEL"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairStrandsVoxelTransmittanceMaskCS, "/Engine/Private/HairStrands/HairStrandsDeepTransmittanceMask.usf", "MainCS", SF_Compute);

// Transmittance mask using voxel volume
static FRDGBufferRef AddHairStrandsVoxelTransmittanceMaskPass(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View,
	const EHairTransmittancePassType PassType,
	const FHairStrandsTransmittanceLightParams& Params,
	const uint32 NodeGroupSize,
	FRDGBufferRef IndirectArgsBuffer,
	FRDGTextureRef ShadowMaskTexture)
{
	check(HairStrands::HasViewHairStrandsVoxelData(View));
	check(NodeGroupSize == 64 || NodeGroupSize == 32);

	const uint32 MaxLightPerPass = 10u; // HAIR_TODO: Need to match the virtual shadow mask bits encoding
	const uint32 AverageLightPerPixel = PassType == EHairTransmittancePassType::OnePass ? MaxLightPerPass : 1u;
	FRDGBufferRef OutBuffer = CreateHairStrandsTransmittanceMaskBuffer(GraphBuilder, View.HairStrandsViewData.VisibilityData.MaxNodeCount * AverageLightPerPixel);

	FHairStrandsVoxelTransmittanceMaskCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairStrandsVoxelTransmittanceMaskCS::FParameters>();
	Parameters->ViewUniformBuffer = View.ViewUniformBuffer;
	Parameters->SceneTextures = SceneTextures;
	Parameters->OutTransmittanceMask = GraphBuilder.CreateUAV(OutBuffer, FHairStrandsTransmittanceMaskData::Format);
	if (PassType == EHairTransmittancePassType::OnePass)
	{
		Parameters->ForwardLightData = View.ForwardLightingResources->ForwardLightDataUniformBuffer;
		Parameters->RayMarchMaskTexture = nullptr;
		Parameters->ShadowMaskBitsTexture = ShadowMaskTexture;
	}
	else
	{
		Parameters->LightDirection = Params.LightDirection;
		Parameters->LightPosition = Params.LightPosition;
		Parameters->LightRadius = Params.LightRadius;
		Parameters->RayMarchMaskTexture = ShadowMaskTexture ? ShadowMaskTexture : GSystemTextures.GetWhiteDummy(GraphBuilder);
		Parameters->ShadowMaskBitsTexture = nullptr;
	}

	Parameters->LightChannelMask = Params.LightChannelMask;
	Parameters->IndirectArgsBuffer = IndirectArgsBuffer;
	Parameters->HairStrands = HairStrands::BindHairStrandsViewUniformParameters(View);
	Parameters->VirtualVoxel = HairStrands::BindHairStrandsVoxelUniformParameters(View);

	if (ShaderDrawDebug::IsShaderDrawDebugEnabled(View))
	{
		ShaderDrawDebug::SetParameters(GraphBuilder, View.ShaderDrawData, Parameters->ShaderDrawParameters);
	}

	if (ShaderPrint::IsSupported(View) && ShaderPrint::IsEnabled())
	{
		ShaderPrint::SetParameters(GraphBuilder, View, Parameters->ShaderPrintUniformBuffer);
	}

	const bool bIsSuperSampled = GHairStrandsTransmittanceSuperSampling > 0;
	const bool bIsMipTraversal = GHairStrandsTransmittanceMaskUseMipTraversal > 0;

	FHairStrandsVoxelTransmittanceMaskCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairStrandsVoxelTransmittanceMaskCS::FTransmittanceGroupSize>(NodeGroupSize);
	PermutationVector.Set<FHairStrandsVoxelTransmittanceMaskCS::FSuperSampling>(bIsSuperSampled ? 1 : 0);
	PermutationVector.Set<FHairStrandsVoxelTransmittanceMaskCS::FTraversal>(bIsMipTraversal ? 1 : 0);
	PermutationVector.Set<FHairStrandsVoxelTransmittanceMaskCS::FOnePass>(PassType == EHairTransmittancePassType::OnePass);
	TShaderMapRef<FHairStrandsVoxelTransmittanceMaskCS> ComputeShader(View.ShaderMap, PermutationVector);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsTransmittanceMask(Voxel,%s)", PassType == EHairTransmittancePassType::OnePass ? TEXT("OnePass") : TEXT("PerLight")),
		ComputeShader,
		Parameters,
		IndirectArgsBuffer,
		0);

	return OutBuffer;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Transmittance Mask from deep shadow

class FHairStrandsDeepShadowTransmittanceMaskCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairStrandsDeepShadowTransmittanceMaskCS);
	SHADER_USE_PARAMETER_STRUCT(FHairStrandsDeepShadowTransmittanceMaskCS, FGlobalShader);

	class FTransmittanceGroupSize : SHADER_PERMUTATION_SPARSE_INT("PERMUTATION_GROUP_SIZE", 32, 64);
	using FPermutationDomain = TShaderPermutationDomain<FTransmittanceGroupSize>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FHairStrandsViewUniformParameters, HairStrands)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)

		SHADER_PARAMETER_ARRAY(FIntVector4, DeepShadow_AtlasSlotOffsets_AtlasSlotIndex, [FHairStrandsDeepShadowData::MaxMacroGroupCount])
		SHADER_PARAMETER_ARRAY(FMatrix44f, DeepShadow_CPUWorldToLightTransforms, [FHairStrandsDeepShadowData::MaxMacroGroupCount])
		SHADER_PARAMETER(FIntPoint, DeepShadow_Resolution)
		SHADER_PARAMETER(float, LightRadius)
		SHADER_PARAMETER(FVector3f, LightDirection)
		SHADER_PARAMETER(FVector4, LightPosition)
		SHADER_PARAMETER(uint32, LightChannelMask)
		SHADER_PARAMETER(FVector4, DeepShadow_LayerDepths)
		SHADER_PARAMETER(float, DeepShadow_DepthBiasScale)
		SHADER_PARAMETER(float, DeepShadow_DensityScale)
		SHADER_PARAMETER(float, DeepShadow_KernelAperture)
		SHADER_PARAMETER(uint32, DeepShadow_KernelType)
		SHADER_PARAMETER(uint32, DeepShadow_DebugMode)
		SHADER_PARAMETER(FMatrix44f, DeepShadow_ShadowToWorld)
		SHADER_PARAMETER(uint32, DeepShadow_bIsGPUDriven)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, RayMarchMaskTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DeepShadow_FrontDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DeepShadow_DomTexture)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, DeepShadow_WorldToLightTransformBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, OutTransmittanceMask)

		RDG_BUFFER_ACCESS(IndirectArgsBuffer, ERHIAccess::IndirectArgs)

	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) 
	{ 
		return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_TRANSMITTANCE_DEEPSHADOW"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairStrandsDeepShadowTransmittanceMaskCS, "/Engine/Private/HairStrands/HairStrandsDeepTransmittanceMask.usf", "MainCS", SF_Compute);

struct FHairStrandsDeepShadowTransmittanceLightParams : FHairStrandsTransmittanceLightParams
{
	FIntVector4 DeepShadow_AtlasSlotOffsets_AtlasSlotIndex[FHairStrandsDeepShadowData::MaxMacroGroupCount];
	FMatrix DeepShadow_CPUWorldToLightTransforms[FHairStrandsDeepShadowData::MaxMacroGroupCount];
	FRDGBufferSRVRef DeepShadow_WorldToLightTransformBuffer = nullptr;
	FIntPoint DeepShadow_Resolution = FIntPoint(0, 0);
	bool DeepShadow_bIsGPUDriven = false;
	FVector4 DeepShadow_LayerDepths = FVector4(0, 0, 0, 0);
	float DeepShadow_DepthBiasScale = 0;
	float DeepShadow_DensityScale = 0;
	FMatrix DeepShadow_ShadowToWorld = FMatrix::Identity;
	
	FRDGTextureRef DeepShadow_FrontDepthTexture = nullptr;
	FRDGTextureRef DeepShadow_DomTexture = nullptr;
};

// Transmittance mask using deep shadow
static FRDGBufferRef AddHairStrandsDeepShadowTransmittanceMaskPass(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View,
	const FHairStrandsDeepShadowTransmittanceLightParams& Params,
	const uint32 NodeGroupSize,
	FRDGBufferRef IndirectArgsBuffer,
	FRDGTextureRef ScreenShadowMaskSubPixelTexture)
{
	FRDGBufferRef OutBuffer = CreateHairStrandsTransmittanceMaskBuffer(GraphBuilder, View.HairStrandsViewData.VisibilityData.MaxNodeCount);

	FHairStrandsDeepShadowTransmittanceMaskCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairStrandsDeepShadowTransmittanceMaskCS::FParameters>();
	Parameters->ViewUniformBuffer = View.ViewUniformBuffer;
	Parameters->SceneTextures = SceneTextures;
	Parameters->DeepShadow_FrontDepthTexture = Params.DeepShadow_FrontDepthTexture;
	Parameters->DeepShadow_DomTexture = Params.DeepShadow_DomTexture;
	Parameters->OutTransmittanceMask = GraphBuilder.CreateUAV(OutBuffer, FHairStrandsTransmittanceMaskData::Format);
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
	Parameters->HairStrands = HairStrands::BindHairStrandsViewUniformParameters(View);
	Parameters->DeepShadow_bIsGPUDriven = Params.DeepShadow_bIsGPUDriven ? 1 : 0;;
	Parameters->DeepShadow_WorldToLightTransformBuffer = Params.DeepShadow_WorldToLightTransformBuffer;
	Parameters->RayMarchMaskTexture = ScreenShadowMaskSubPixelTexture ? ScreenShadowMaskSubPixelTexture : GraphBuilder.RegisterExternalTexture(GSystemTextures.WhiteDummy);

	memcpy(&(Parameters->DeepShadow_AtlasSlotOffsets_AtlasSlotIndex[0]), Params.DeepShadow_AtlasSlotOffsets_AtlasSlotIndex, sizeof(FIntVector4) * FHairStrandsDeepShadowData::MaxMacroGroupCount);
	memcpy(&(Parameters->DeepShadow_CPUWorldToLightTransforms[0]), Params.DeepShadow_CPUWorldToLightTransforms, sizeof(FMatrix) * FHairStrandsDeepShadowData::MaxMacroGroupCount);

	check(NodeGroupSize == 64 || NodeGroupSize == 32);
	FHairStrandsDeepShadowTransmittanceMaskCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairStrandsDeepShadowTransmittanceMaskCS::FTransmittanceGroupSize>(NodeGroupSize);

	TShaderMapRef<FHairStrandsDeepShadowTransmittanceMaskCS> ComputeShader(View.ShaderMap, PermutationVector);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsTransmittanceMask(DeepShadow)"),
		ComputeShader,
		Parameters,
		IndirectArgsBuffer,
		0);

	return OutBuffer;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Opaque Mask from voxel volume

class FHairStrandsVoxelShadowMaskPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairStrandsVoxelShadowMaskPS);
	SHADER_USE_PARAMETER_STRUCT(FHairStrandsVoxelShadowMaskPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderDrawDebug::FShaderDrawDebugParameters, ShaderDrawParameters)
		
		SHADER_PARAMETER(FVector4, Voxel_LightPosition)
		SHADER_PARAMETER(FVector3f, Voxel_LightDirection)
		SHADER_PARAMETER(uint32, Voxel_MacroGroupId)
		SHADER_PARAMETER(uint32, Voxel_RandomType)
		SHADER_PARAMETER(uint32, bIsWholeSceneLight)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, RayMarchMaskTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, ShadowSampler)

		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualVoxelParameters, VirtualVoxel)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FHairStrandsViewUniformParameters, HairStrands)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetRenderTargetOutputFormat(0, PF_B8G8R8A8);
		OutEnvironment.SetDefine(TEXT("SHADER_SHADOWMASK_VOXEL"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairStrandsVoxelShadowMaskPS, "/Engine/Private/HairStrands/HairStrandsDeepShadowMask.usf", "MainPS", SF_Pixel);

struct FHairStrandsVoxelShadowParams
{
	bool			bIsWholeSceneLight = false;
	FVector			Voxel_LightDirection = FVector::ZeroVector;
	FVector4		Voxel_LightPosition = FVector4(0, 0, 0, 0);
	uint32			Voxel_MacroGroupId;
};

// Opaque mask from voxel
static void AddHairStrandsVoxelShadowMaskPass(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef SceneDepthTexture,
	const FViewInfo& View,
	const FHairStrandsVoxelShadowParams& Params,
	FRDGTextureRef& OutShadowMask)
{
	check(OutShadowMask);
	check(HairStrands::HasViewHairStrandsVoxelData(View));

	FHairStrandsVoxelShadowMaskPS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairStrandsVoxelShadowMaskPS::FParameters>();
	Parameters->ViewUniformBuffer = View.ViewUniformBuffer;
	Parameters->SceneDepthTexture = SceneDepthTexture;	
	Parameters->HairStrands = HairStrands::BindHairStrandsViewUniformParameters(View);
	Parameters->VirtualVoxel = HairStrands::BindHairStrandsVoxelUniformParameters(View);
	Parameters->LinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters->ShadowSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters->bIsWholeSceneLight = Params.bIsWholeSceneLight ? 1 : 0;
	Parameters->Voxel_LightPosition		= Params.Voxel_LightPosition;
	Parameters->Voxel_LightDirection	= Params.Voxel_LightDirection;
	Parameters->Voxel_MacroGroupId		= Params.Voxel_MacroGroupId;
	Parameters->Voxel_RandomType = FMath::Clamp(GHairStrandsShadowRandomTraversalType, 0, 2);	
	Parameters->RenderTargets[0] = FRenderTargetBinding(OutShadowMask, ERenderTargetLoadAction::ELoad);

	if (ShaderDrawDebug::IsShaderDrawDebugEnabled(View))
	{
		ShaderDrawDebug::SetParameters(GraphBuilder, View.ShaderDrawData, Parameters->ShaderDrawParameters);
	}

	FRDGTextureRef RayMarchMask = nullptr;
	{
		FRDGTextureDesc Desc = OutShadowMask->Desc;
		Desc.Flags |= TexCreate_ShaderResource;
		RayMarchMask = GraphBuilder.CreateTexture(Desc, TEXT("Hair.RayMarchMask"));
		FRHICopyTextureInfo CopyInfo;
		CopyInfo.Size = OutShadowMask->Desc.GetSize();
		AddCopyTexturePass(GraphBuilder, OutShadowMask, RayMarchMask, CopyInfo);
	}
	Parameters->RayMarchMaskTexture = RayMarchMask;


	FHairStrandsVoxelShadowMaskPS::FPermutationDomain PermutationVector;

	const FIntPoint OutputResolution = SceneDepthTexture->Desc.Extent;
	TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
	TShaderMapRef<FHairStrandsVoxelShadowMaskPS> PixelShader(View.ShaderMap, PermutationVector);
	const FGlobalShaderMap* GlobalShaderMap = View.ShaderMap;
	const FIntRect Viewport = View.ViewRect;

	ClearUnusedGraphResources(PixelShader, Parameters);
	FIntPoint Resolution = OutShadowMask->Desc.Extent;

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HairStrandsShadowMask(Voxel)"),
		Parameters,
		ERDGPassFlags::Raster,
		[Parameters, VertexShader, PixelShader, Viewport, Resolution](FRHICommandList& RHICmdList)
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

///////////////////////////////////////////////////////////////////////////////////////////////////
// Opaque Mask from deep shadow

class FHairStrandsDeepShadowMaskPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairStrandsDeepShadowMaskPS);
	SHADER_USE_PARAMETER_STRUCT(FHairStrandsDeepShadowMaskPS, FGlobalShader);

	class FKernelType : SHADER_PERMUTATION_INT("PERMUTATION_KERNEL_TYPE", 5);
	using FPermutationDomain = TShaderPermutationDomain<FKernelType>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderDrawDebug::FShaderDrawDebugParameters, ShaderDrawParameters)
		                         
		SHADER_PARAMETER(FIntPoint, DeepShadow_SlotOffset)
		SHADER_PARAMETER(uint32, DeepShadow_SlotIndex)
		SHADER_PARAMETER(FIntPoint, DeepShadow_SlotResolution)
		SHADER_PARAMETER(FMatrix44f, DeepShadow_CPUWorldToLightTransform)
		SHADER_PARAMETER(uint32, bIsWholeSceneLight)
		SHADER_PARAMETER(float, DeepShadow_DepthBiasScale)
		SHADER_PARAMETER(float, DeepShadow_DensityScale)
		SHADER_PARAMETER(uint32, DeepShadow_bIsGPUDriven)
		SHADER_PARAMETER(FVector4, DeepShadow_LayerDepths)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, RayMarchMaskTexture)

		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer, DeepShadow_WorldToLightTransformBuffer)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DeepShadow_FrontDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DeepShadow_DomTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, ShadowSampler)

		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FHairStrandsViewUniformParameters, HairStrands)
		RENDER_TARGET_BINDING_SLOTS()
		END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetRenderTargetOutputFormat(0, PF_B8G8R8A8);
		OutEnvironment.SetDefine(TEXT("SHADER_SHADOWMASK_DEEPSHADOW"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairStrandsDeepShadowMaskPS, "/Engine/Private/HairStrands/HairStrandsDeepShadowMask.usf", "MainPS", SF_Pixel);


struct FHairStrandsDeepShadowParams
{
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
};

// Opaque mask with deep shadow
static void AddHairStrandsDeepShadowMaskPass(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef SceneDepthTexture,
	const FViewInfo& View,
	const FHairStrandsDeepShadowParams& Params,
	FRDGTextureRef& OutShadowMask)
{
	check(OutShadowMask);

	FHairStrandsDeepShadowMaskPS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairStrandsDeepShadowMaskPS::FParameters>();
	Parameters->ViewUniformBuffer = View.ViewUniformBuffer;
	Parameters->SceneDepthTexture = SceneDepthTexture;
	Parameters->DeepShadow_CPUWorldToLightTransform = Params.DeepShadow_CPUWorldToLightTransform;
	Parameters->DeepShadow_FrontDepthTexture = Params.DeepShadow_FrontDepthTexture;
	Parameters->DeepShadow_DomTexture = Params.DeepShadow_LayerTexture;
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
	Parameters->HairStrands = HairStrands::BindHairStrandsViewUniformParameters(View);
	if (ShaderDrawDebug::IsShaderDrawDebugEnabled(View))
	{
		ShaderDrawDebug::SetParameters(GraphBuilder, View.ShaderDrawData, Parameters->ShaderDrawParameters);
	}

	FRDGTextureRef RayMarchMask = nullptr;
	{
		FRDGTextureDesc Desc = OutShadowMask->Desc;
		Desc.Flags |= TexCreate_ShaderResource;
		RayMarchMask = GraphBuilder.CreateTexture(Desc, TEXT("Hair.RayMarchMask"));
		FRHICopyTextureInfo CopyInfo;
		CopyInfo.Size = OutShadowMask->Desc.GetSize();
		AddCopyTexturePass(GraphBuilder, OutShadowMask, RayMarchMask, CopyInfo);
	}
	Parameters->RayMarchMaskTexture = RayMarchMask;

	FHairStrandsDeepShadowMaskPS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairStrandsDeepShadowMaskPS::FKernelType>(FMath::Clamp(GStrandHairShadowMaskKernelType, 0, 4));

	const FIntPoint OutputResolution = SceneDepthTexture->Desc.Extent;
	TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
	TShaderMapRef<FHairStrandsDeepShadowMaskPS> PixelShader(View.ShaderMap, PermutationVector);
	const FGlobalShaderMap* GlobalShaderMap = View.ShaderMap;
	const FIntRect Viewport = View.ViewRect;

	ClearUnusedGraphResources(PixelShader, Parameters);
	FIntPoint Resolution = OutShadowMask->Desc.Extent;

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("HairStrandsShadowMask"),
		Parameters,
		ERDGPassFlags::Raster,
		[Parameters, VertexShader, PixelShader, Viewport, Resolution](FRHICommandList& RHICmdList)
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

///////////////////////////////////////////////////////////////////////////////////////////////////

static FHairStrandsTransmittanceMaskData InternalRenderHairStrandsTransmittanceMask(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FLightSceneInfo* LightSceneInfo,
	const FHairStrandsVisibilityData& VisibilityData,
	const FHairStrandsMacroGroupDatas& MacroGroupDatas,
	const FHairStrandsDeepShadowResources& DeepShadowResources,
	const FHairStrandsVoxelResources& VoxelResources,
	FRDGTextureRef ScreenShadowMaskSubPixelTexture)
{
	FHairStrandsTransmittanceMaskData Out;
	if (MacroGroupDatas.Num() == 0)
		return Out;

	if (!HasDeepShadowData(LightSceneInfo, MacroGroupDatas) && !IsHairStrandsVoxelizationEnable())
		return Out;

	DECLARE_GPU_STAT(HairStrandsTransmittanceMask);
	RDG_EVENT_SCOPE(GraphBuilder, "HairStrandsTransmittanceMask");
	RDG_GPU_STAT_SCOPE(GraphBuilder, HairStrandsTransmittanceMask);

	// Note: GbufferB.a store the shading model on the 4 lower bits (MATERIAL_SHADINGMODEL_HAIR)
	FSceneTextureParameters SceneTextures = GetSceneTextureParameters(GraphBuilder);

	bool bHasFoundLight = false;
	if (!IsHairStrandsForVoxelTransmittanceAndShadowEnable())
	{
		FHairStrandsDeepShadowTransmittanceLightParams Params;
		Params.DeepShadow_DensityScale = GetDeepShadowDensityScale();
		Params.DeepShadow_DepthBiasScale = GetDeepShadowDepthBiasScale();
		memset(Params.DeepShadow_AtlasSlotOffsets_AtlasSlotIndex, 0, sizeof(Params.DeepShadow_AtlasSlotOffsets_AtlasSlotIndex));
		memset(Params.DeepShadow_CPUWorldToLightTransforms, 0, sizeof(Params.DeepShadow_CPUWorldToLightTransforms));

		FRDGBufferSRVRef DeepShadow_WorldToLightTransformBufferSRV = nullptr;
		for (const FHairStrandsMacroGroupData& MacroGroupData : MacroGroupDatas)
		{
			for (const FHairStrandsDeepShadowData& DeepShadowData : MacroGroupData.DeepShadowDatas)
			{
				if (DeepShadowData.LightId == LightSceneInfo->Id)
				{
					if (DeepShadow_WorldToLightTransformBufferSRV == nullptr)
					{
						DeepShadow_WorldToLightTransformBufferSRV = GraphBuilder.CreateSRV(DeepShadowResources.DeepShadowWorldToLightTransforms);
					}

					bHasFoundLight = true;
					Params.DeepShadow_FrontDepthTexture = DeepShadowResources.DepthAtlasTexture;
					Params.DeepShadow_DomTexture = DeepShadowResources.LayersAtlasTexture;
					Params.DeepShadow_Resolution = DeepShadowData.ShadowResolution;
					Params.LightDirection = DeepShadowData.LightDirection;
					Params.LightPosition = DeepShadowData.LightPosition;
					Params.LightRadius = 0;
					Params.LightChannelMask = LightSceneInfo->Proxy->GetLightingChannelMask();
					Params.DeepShadow_LayerDepths = ComputeDeepShadowLayerDepths(DeepShadowData.LayerDistribution);
					Params.DeepShadow_AtlasSlotOffsets_AtlasSlotIndex[DeepShadowData.MacroGroupId] = FIntVector4(DeepShadowData.AtlasRect.Min.X, DeepShadowData.AtlasRect.Min.Y, DeepShadowData.AtlasSlotIndex, 0);
					Params.DeepShadow_CPUWorldToLightTransforms[DeepShadowData.MacroGroupId] = DeepShadowData.CPU_WorldToLightTransform;
					Params.DeepShadow_WorldToLightTransformBuffer = DeepShadow_WorldToLightTransformBufferSRV;
					Params.DeepShadow_bIsGPUDriven = DeepShadowResources.bIsGPUDriven;
				}
			}
		}

		if (bHasFoundLight)
		{
			check(Params.DeepShadow_FrontDepthTexture);
			check(Params.DeepShadow_DomTexture);
			Out.TransmittanceMask = AddHairStrandsDeepShadowTransmittanceMaskPass(
				GraphBuilder,
				SceneTextures,
				View,
				Params,
				VisibilityData.NodeGroupSize,
				VisibilityData.NodeIndirectArg,
				ScreenShadowMaskSubPixelTexture);
		}
	}

	if (!bHasFoundLight && VoxelResources.IsValid())
	{
		FLightShaderParameters LightParameters;
		LightSceneInfo->Proxy->GetLightShaderParameters(LightParameters);

		FHairStrandsTransmittanceLightParams Params;
		Params.LightDirection = LightSceneInfo->Proxy->GetDirection();
		Params.LightPosition = FVector4(FVector(LightSceneInfo->Proxy->GetPosition()), LightSceneInfo->Proxy->GetLightType() == ELightComponentType::LightType_Directional ? 0 : 1);
		Params.LightChannelMask = LightSceneInfo->Proxy->GetLightingChannelMask();
		Params.LightRadius = FMath::Max(LightParameters.SourceLength, LightParameters.SourceRadius);

		Out.TransmittanceMask = AddHairStrandsVoxelTransmittanceMaskPass(
			GraphBuilder,
			SceneTextures,
			View,
			EHairTransmittancePassType::PerLight,
			Params,
			VisibilityData.NodeGroupSize,
			VisibilityData.NodeIndirectArg,
			ScreenShadowMaskSubPixelTexture);
	}

	return Out;
}
	
FHairStrandsTransmittanceMaskData RenderHairStrandsOnePassTransmittanceMask(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View, 
	FRDGTextureRef ShadowMaskBits)
{
	FHairStrandsTransmittanceMaskData Out;
	if (HairStrands::HasViewHairStrandsData(View) && View.HairStrandsViewData.MacroGroupDatas.Num() > 0)
	{
		DECLARE_GPU_STAT(HairStrandsOnePassTransmittanceMask);
		RDG_EVENT_SCOPE(GraphBuilder, "HairStrandsTransmittanceMask(OnePass)");
		RDG_GPU_STAT_SCOPE(GraphBuilder, HairStrandsOnePassTransmittanceMask);

		if (HairStrands::HasViewHairStrandsVoxelData(View))
		{
			// Note: GbufferB.a store the shading model on the 4 lower bits (MATERIAL_SHADINGMODEL_HAIR)
			FSceneTextureParameters SceneTextures = GetSceneTextureParameters(GraphBuilder);
			FHairStrandsTransmittanceLightParams DummyParams;

			Out.TransmittanceMask = AddHairStrandsVoxelTransmittanceMaskPass(
				GraphBuilder,
				SceneTextures,
				View,
				EHairTransmittancePassType::OnePass,
				DummyParams,
				View.HairStrandsViewData.VisibilityData.NodeGroupSize,
				View.HairStrandsViewData.VisibilityData.NodeIndirectArg,
				ShadowMaskBits);
		}
	}
	return Out;
}

FHairStrandsTransmittanceMaskData RenderHairStrandsTransmittanceMask(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FLightSceneInfo* LightSceneInfo,
	FRDGTextureRef ScreenShadowMaskSubPixelTexture)
{
	FHairStrandsTransmittanceMaskData TransmittanceMaskData;
	if (HairStrands::HasViewHairStrandsData(View))
	{
		TransmittanceMaskData = InternalRenderHairStrandsTransmittanceMask(
			GraphBuilder, 
			View, 
			LightSceneInfo, 
			View.HairStrandsViewData.VisibilityData,
			View.HairStrandsViewData.MacroGroupDatas,
			View.HairStrandsViewData.DeepShadowResources,
			View.HairStrandsViewData.VirtualVoxelResources,
			ScreenShadowMaskSubPixelTexture);
	}
	return TransmittanceMaskData;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

static void InternalRenderHairStrandsShadowMask(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FLightSceneInfo* LightSceneInfo,
	const FHairStrandsVisibilityData& InVisibilityData,
	const FHairStrandsMacroGroupDatas& InMacroGroupDatas,
	const FHairStrandsDeepShadowResources& DeepShadowResources,
	const FHairStrandsVoxelResources& VoxelResources,
	FRDGTextureRef OutShadowMask)
{
	if (InMacroGroupDatas.Num() == 0)
		return;

	if (!HasDeepShadowData(LightSceneInfo, InMacroGroupDatas) && !IsHairStrandsVoxelizationEnable())
		return;

	DECLARE_GPU_STAT(HairStrandsOpaqueMask);
	RDG_EVENT_SCOPE(GraphBuilder, "HairStrandsOpaqueMask");
	RDG_GPU_STAT_SCOPE(GraphBuilder, HairStrandsOpaqueMask);
	const FMinimalSceneTextures& SceneTextures = FSceneTextures::Get(GraphBuilder);

	bool bHasDeepShadow = false;
	if (!IsHairStrandsForVoxelTransmittanceAndShadowEnable())
	{
		FRDGBufferRef DeepShadow_WorldToLightTransformBuffer = nullptr;
		FRDGBufferSRVRef DeepShadow_WorldToLightTransformBufferSRV = nullptr;

		for (const FHairStrandsMacroGroupData& MacroGroupData : InMacroGroupDatas)
		{
			for (const FHairStrandsDeepShadowData& DomData : MacroGroupData.DeepShadowDatas)
			{
				if (DomData.LightId != LightSceneInfo->Id)
					continue;

				if (DeepShadow_WorldToLightTransformBuffer == nullptr)
				{
					DeepShadow_WorldToLightTransformBuffer = DeepShadowResources.DeepShadowWorldToLightTransforms;
					DeepShadow_WorldToLightTransformBufferSRV = GraphBuilder.CreateSRV(DeepShadow_WorldToLightTransformBuffer);
				}

				bHasDeepShadow = true;
				const bool bIsWholeSceneLight = LightSceneInfo->Proxy->GetLightType() == LightType_Directional;

				FHairStrandsDeepShadowParams Params;
				Params.DeepShadow_AtlasSlotIndex = DomData.AtlasSlotIndex;
				Params.DeepShadow_WorldToLightTransformBuffer = DeepShadow_WorldToLightTransformBufferSRV;
				Params.DeepShadow_bIsGPUDriven = DeepShadowResources.bIsGPUDriven ? 1 : 0;
				Params.DeepShadow_CPUWorldToLightTransform = DomData.CPU_WorldToLightTransform;
				Params.DeepShadow_AtlasRect = DomData.AtlasRect;
				Params.DeepShadow_FrontDepthTexture = DeepShadowResources.DepthAtlasTexture;
				Params.DeepShadow_LayerTexture = DeepShadowResources.LayersAtlasTexture;
				Params.bIsWholeSceneLight = bIsWholeSceneLight;
				Params.DeepShadow_DepthBiasScale = GetDeepShadowDepthBiasScale();
				Params.DeepShadow_DensityScale = GetDeepShadowDensityScale();
				Params.DeepShadow_LayerDepths = ComputeDeepShadowLayerDepths(DomData.LayerDistribution);
				AddHairStrandsDeepShadowMaskPass(
					GraphBuilder,
					SceneTextures.Depth.Resolve,
					View,
					Params,
					OutShadowMask);
			}
		}
	}

	// Code is disabled for now until we have the full DOM/voxel fallback logic
	// If there is no deep shadow for this light, fallback on the voxel representation
	if (!bHasDeepShadow && HairStrands::HasViewHairStrandsVoxelData(View))
	{
		// TODO: Change this to be a single pass with virtual voxel?
		for (const FHairStrandsMacroGroupData& MacroGroupData : InMacroGroupDatas)
		{
			const bool bIsWholeSceneLight = LightSceneInfo->Proxy->GetLightType() == LightType_Directional;

			FHairStrandsVoxelShadowParams Params;
			Params.Voxel_LightDirection = LightSceneInfo->Proxy->GetDirection();
			Params.Voxel_LightPosition = FVector4(FVector(LightSceneInfo->Proxy->GetPosition()), LightSceneInfo->Proxy->GetLightType() == ELightComponentType::LightType_Directional ? 0 : 1);
			Params.Voxel_MacroGroupId = MacroGroupData.MacroGroupId;
			Params.bIsWholeSceneLight = bIsWholeSceneLight ? 1 : 0;

			AddHairStrandsVoxelShadowMaskPass(
				GraphBuilder,
				SceneTextures.Depth.Resolve,
				View,
				Params,
				OutShadowMask);
		}
	}
}

void RenderHairStrandsShadowMask(
	FRDGBuilder& GraphBuilder,
	const TArray<FViewInfo>& Views,
	const FLightSceneInfo* LightSceneInfo,
	FRDGTextureRef OutShadowMask)
{
	if (Views.Num() == 0 || OutShadowMask == nullptr)
	{
		return;
	}

	for (const FViewInfo& View : Views)
	{
		if (HairStrands::HasViewHairStrandsData(View))
		{
			check(View.HairStrandsViewData.VisibilityData.CategorizationTexture);
			InternalRenderHairStrandsShadowMask(
				GraphBuilder, 
				View, 
				LightSceneInfo, 
				View.HairStrandsViewData.VisibilityData,
				View.HairStrandsViewData.MacroGroupDatas,
				View.HairStrandsViewData.DeepShadowResources,
				View.HairStrandsViewData.VirtualVoxelResources,
				OutShadowMask);
		}
	}
}