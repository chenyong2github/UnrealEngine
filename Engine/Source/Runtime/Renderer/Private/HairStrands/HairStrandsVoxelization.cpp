// Copyright Epic Games, Inc. All Rights Reserved.

#include "HairStrandsVoxelization.h"
#include "HairStrandsRasterCommon.h"
#include "HairStrandsCluster.h"
#include "HairStrandsUtils.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "ShaderParameters.h"
#include "ShaderParameterStruct.h"
#include "SceneTextureParameters.h"
#include "RenderGraphUtils.h"
#include "PostProcessing.h"

static float GStrandHairVoxelizationRasterizationScale = 1.0f;
static FAutoConsoleVariableRef CVarStrandHairVoxelizationRasterizationScale(TEXT("r.HairStrands.VoxelizationRasterizationScale"), GStrandHairVoxelizationRasterizationScale, TEXT("Rasterization scale to snap strand to pixel for voxelization"));

static int32 GHairVoxelizationEnable = 1;
static FAutoConsoleVariableRef CVarGHairVoxelizationEnable(TEXT("r.HairStrands.Voxelization"), GHairVoxelizationEnable, TEXT("Enable hair voxelization for transmittance evaluation"));

static float GHairVoxelizationAABBScale = 1.0f;
static FAutoConsoleVariableRef CVarHairVoxelizationAABBScale(TEXT("r.HairStrands.Voxelization.AABBScale"), GHairVoxelizationAABBScale, TEXT("Scale the hair macro group bounding box"));

static float GHairVoxelizationDensityScale = 2.0f;
static float GHairVoxelizationDepthBiasScale = 3.0f;
static FAutoConsoleVariableRef CVarHairVoxelizationDensityScale(TEXT("r.HairStrands.Voxelization.DensityScale"), GHairVoxelizationDensityScale, TEXT("Scale the hair density when computing voxel transmittance. Default value is 2 (arbitraty)"));
static FAutoConsoleVariableRef CVarHairVoxelizationDepthBiasScale(TEXT("r.HairStrands.Voxelization.DepthBiasScale"), GHairVoxelizationDepthBiasScale, TEXT("Set depth bias for voxel ray marching. Offset the origin position towards the light"));

static int32 GHairVoxelInjectOpaqueDepthEnable = 1;
static FAutoConsoleVariableRef CVarHairVoxelInjectOpaqueDepthEnable(TEXT("r.HairStrands.Voxelization.InjectOpaqueDepth"), GHairVoxelInjectOpaqueDepthEnable, TEXT("Inject opaque geometry depth into the voxel volume for acting as occluder."));

static int32 GHairStransVoxelInjectOpaqueBiasCount = 3;
static int32 GHairStransVoxelInjectOpaqueMarkCount = 6;
static FAutoConsoleVariableRef CVarHairStransVoxelInjectOpaqueBiasCount(TEXT("r.HairStrands.Voxelization.InjectOpaque.BiasCount"), GHairStransVoxelInjectOpaqueBiasCount, TEXT("Bias, in number of voxel, at which opaque depth is injected."));
static FAutoConsoleVariableRef CVarHairStransVoxelInjectOpaqueMarkCount(TEXT("r.HairStrands.Voxelization.InjectOpaque.MarkCount"), GHairStransVoxelInjectOpaqueMarkCount, TEXT("Number of voxel marked as opaque starting along the view direction beneath the opaque surface."));

static float GHairStransVoxelRaymarchingSteppingScale = 1.15f;
static FAutoConsoleVariableRef CVarHairStransVoxelRaymarchingSteppingScale(TEXT("r.HairStrands.Voxelization.Raymarching.SteppingScale"), GHairStransVoxelRaymarchingSteppingScale, TEXT("Stepping scale used for raymarching the voxel structure."));

static float GetHairStrandsVoxelizationDensityScale() { return FMath::Max(0.0f, GHairVoxelizationDensityScale); }
static float GetHairStrandsVoxelizationDepthBiasScale() { return FMath::Max(0.0f, GHairVoxelizationDepthBiasScale); }

static int32 GHairForVoxelTransmittanceAndShadow = 0;
static FAutoConsoleVariableRef CVarHairForVoxelTransmittanceAndShadow(TEXT("r.HairStrands.Voxelization.ForceTransmittanceAndShadow"), GHairForVoxelTransmittanceAndShadow, TEXT("For transmittance and shadow to be computed with density volume. This requires voxelization is enabled."));

static int32 GHairVirtualVoxel = 1;
static float GHairVirtualVoxel_VoxelWorldSize = 0.3f; // 3.0mm
static int32 GHairVirtualVoxel_PageResolution = 32;
static int32 GHairVirtualVoxel_PageCountPerDim = 14;
static FAutoConsoleVariableRef CVarHairVirtualVoxel(TEXT("r.HairStrands.Voxelization.Virtual"), GHairVirtualVoxel, TEXT("Enable the two voxel hierachy."));
static FAutoConsoleVariableRef CVarHairVirtualVoxel_VoxelWorldSize(TEXT("r.HairStrands.Voxelization.Virtual.VoxelWorldSize"), GHairVirtualVoxel_VoxelWorldSize, TEXT("World size of a voxel in cm."));
static FAutoConsoleVariableRef CVarHairVirtualVoxel_VoxelPageResolution(TEXT("r.HairStrands.Voxelization.Virtual.VoxelPageResolution"), GHairVirtualVoxel_PageResolution, TEXT("Resolution of a voxel page."));
static FAutoConsoleVariableRef CVarHairVirtualVoxel_VoxelPageCountPerDim(TEXT("r.HairStrands.Voxelization.Virtual.VoxelPageCountPerDim"), GHairVirtualVoxel_PageCountPerDim, TEXT("Number of voxel pages per texture dimension. The voxel page memory is allocated with a 3D texture. This value provide the resolution of this texture."));

static int32 GHairVirtualVoxelGPUDriven = 1;
static int32 GHairVirtualVoxelGPUDrivenMaxPageIndexRes = 32;
static FAutoConsoleVariableRef CVarHairVirtualVoxelGPUDriven(TEXT("r.HairStrands.Voxelization.GPUDriven"), GHairVirtualVoxelGPUDriven, TEXT("Enable GPU driven voxelization."));
static FAutoConsoleVariableRef CVarHairVirtualVoxelGPUDrivenMaxPageIndexRes(TEXT("r.HairStrands.Voxelization.GPUDriven.MaxPageIndexResolution"), GHairVirtualVoxelGPUDrivenMaxPageIndexRes, TEXT("Max resolution of the page index. This is used for allocating a conservative page index buffer when GPU driven allocation is enabled."));

static int32 GHairVirtualVoxelUseIndirectScatterPageAllocation = 1;
static FAutoConsoleVariableRef CVarHairVirtualVoxelUseIndirectScatterPageAllocation(TEXT("r.HairStrands.Voxelization.UseIndiretScatterPageAllocate"), GHairVirtualVoxelUseIndirectScatterPageAllocation, TEXT("Enable indirect scatter page allocation (faster)."));

static const FIntPoint GPUDrivenViewportResolution = FIntPoint(4096, 4096);

static int32 GHairVirtualVoxelInvalidEmptyPageIndex = 1;
static FAutoConsoleVariableRef CVarHairVirtualVoxelInvalidEmptyPageIndex(TEXT("r.HairStrands.Voxelization.Virtual.InvalidateEmptyPageIndex"), GHairVirtualVoxelInvalidEmptyPageIndex, TEXT("Invalid voxel page index which does not contain any voxelized data."));

static int32 GHairStrandsVoxelComputeRaster = 1;
static FAutoConsoleVariableRef CVarHairStrandsVoxelComputeRaster(TEXT("r.HairStrands.Voxelization.Virtual.ComputeRaster"), GHairStrandsVoxelComputeRaster, TEXT("Use compute for rasterizing voxeliation (faster)."));

static int32 GHairStrandsVoxelComputeRasterMaxVoxelCount = 32;
static FAutoConsoleVariableRef CVarHairStrandsVoxelComputeRasterMaxVoxelCount(TEXT("r.HairStrands.Voxelization.Virtual.ComputeRasterMaxVoxelCount"), GHairStrandsVoxelComputeRasterMaxVoxelCount, TEXT("Max number of voxel which are rasterized for a given hair segment. This is for debug purpose only."));

bool IsHairStrandsVoxelizationEnable()
{
	return GHairVoxelizationEnable > 0;
}

bool IsHairStrandsForVoxelTransmittanceAndShadowEnable()
{
	return IsHairStrandsVoxelizationEnable() && GHairForVoxelTransmittanceAndShadow > 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FVirtualVoxelParameters, "VirtualVoxel");

///////////////////////////////////////////////////////////////////////////////////////////////////
class FVirtualVoxelInjectOpaqueCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVirtualVoxelInjectOpaqueCS);
	SHADER_USE_PARAMETER_STRUCT(FVirtualVoxelInjectOpaqueCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_STRUCT(FVirtualVoxelCommonParameters, VirtualVoxelParams)
		SHADER_PARAMETER(FIntVector, DispatchedPageIndexResolution)
		SHADER_PARAMETER(uint32, MacroGroupId)
		SHADER_PARAMETER(FVector2D, SceneDepthResolution)
		SHADER_PARAMETER(uint32, VoxelBiasCount)
		SHADER_PARAMETER(uint32, VoxelMarkCount)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTexture)
		SHADER_PARAMETER_RDG_BUFFER(StructuredBuffer, IndirectDispatchArgs)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D, OutPageTexture)

		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_INJECTOPAQUE_VIRTUALVOXEL"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FVirtualVoxelInjectOpaqueCS, "/Engine/Private/HairStrands/HairStrandsVoxelOpaque.usf", "MainCS", SF_Compute);

static void AddVirtualVoxelInjectOpaquePass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FVirtualVoxelResources& VoxelResources,
	const FHairStrandsMacroGroupData& MacroGroup)
{
	FSceneTextureParameters SceneTextures;
	SetupSceneTextureParameters(GraphBuilder, &SceneTextures);

	const FRDGTextureRef OutPageTexture = GraphBuilder.RegisterExternalTexture(VoxelResources.PageTexture, TEXT("HairVoxelPageTexture"));
	FRDGBufferRef IndirectDispatchArgsBuffer = GraphBuilder.RegisterExternalBuffer(VoxelResources.IndirectArgsBuffer, TEXT("HairVoxelIndirectDispatchArgs"));

	const uint32 TotalPageCount = VoxelResources.Parameters.Common.PageIndexCount;
	const uint32 PageResolution = VoxelResources.Parameters.Common.PageResolution;

	const uint32 SideSlotCount = FMath::CeilToInt(FMath::Pow(TotalPageCount, 1.f / 3.f));
	const uint32 SideVoxelCount= SideSlotCount * PageResolution;

	FVirtualVoxelInjectOpaqueCS::FParameters* Parameters = GraphBuilder.AllocParameters<FVirtualVoxelInjectOpaqueCS::FParameters>();
	Parameters->ViewUniformBuffer = View.ViewUniformBuffer;
	Parameters->VirtualVoxelParams = VoxelResources.Parameters.Common;
	Parameters->VoxelBiasCount = FMath::Max(0, GHairStransVoxelInjectOpaqueBiasCount);
	Parameters->VoxelMarkCount = FMath::Max(0, GHairStransVoxelInjectOpaqueMarkCount);
	Parameters->SceneDepthResolution = SceneTextures.SceneDepthBuffer->Desc.Extent;
	Parameters->SceneDepthTexture = SceneTextures.SceneDepthBuffer;
	Parameters->SceneTextures = SceneTextures;
	Parameters->MacroGroupId = MacroGroup.MacroGroupId;
	Parameters->OutPageTexture = GraphBuilder.CreateUAV(OutPageTexture);
	Parameters->DispatchedPageIndexResolution = MacroGroup.VirtualVoxelNodeDesc.PageIndexResolution;
	Parameters->IndirectDispatchArgs = IndirectDispatchArgsBuffer;
	TShaderMapRef<FVirtualVoxelInjectOpaqueCS> ComputeShader(View.ShaderMap);
	const FGlobalShaderMap* GlobalShaderMap = View.ShaderMap;

	check(VoxelResources.Parameters.Common.IndirectDispatchGroupSize == 64);
	const uint32 ArgsOffset = sizeof(uint32) * 3 * Parameters->MacroGroupId;

	FComputeShaderUtils::AddPass(
		GraphBuilder, 
		RDG_EVENT_NAME("HairStrandsInjectOpaqueDepthInVoxel"), 
		ComputeShader, 
		Parameters, 
		IndirectDispatchArgsBuffer, 
		ArgsOffset);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
class FVoxelAllocatePageIndexCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVoxelAllocatePageIndexCS);
	SHADER_USE_PARAMETER_STRUCT(FVoxelAllocatePageIndexCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(float, PageWorldSize)
		SHADER_PARAMETER(uint32, TotalPageIndexCount)
		SHADER_PARAMETER(uint32, PageResolution)
		SHADER_PARAMETER(uint32, MacroGroupCount)
		SHADER_PARAMETER(uint32, IndirectDispatchGroupSize)

		SHADER_PARAMETER(FVector, CPU_MinAABB)
		SHADER_PARAMETER(FVector, CPU_MaxAABB)
		SHADER_PARAMETER(FIntVector, CPU_PageIndexResolution)
		SHADER_PARAMETER(uint32, CPU_bUseCPUData)

		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<int>, MacroGroupAABBBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint4>, OutPageIndexResolutionAndOffsetBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, OutVoxelizationViewInfoBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, OutPageIndexAllocationIndirectBufferArgs)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_ALLOCATEPAGEINDEX"), 1);
	}
};

class FVoxelMarkValidPageIndex_PrepareCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVoxelMarkValidPageIndex_PrepareCS);
	SHADER_USE_PARAMETER_STRUCT(FVoxelMarkValidPageIndex_PrepareCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, MaxClusterCount)
		SHADER_PARAMETER(uint32, MacroGroupId)
		SHADER_PARAMETER(uint32, MaxScatterAllocationCount)
		SHADER_PARAMETER_SRV(Buffer, ClusterAABBsBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, MacroGroupAABBBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, PageIndexResolutionAndOffsetBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(Buffer<uint>, OutValidPageIndexBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutDeferredScatterCounter)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint2>, OutDeferredScatterBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_MARKVALID_PREPARE"), 1);
	}
};

class FVoxelMarkValidPageIndex_IndirectArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVoxelMarkValidPageIndex_IndirectArgsCS);
	SHADER_USE_PARAMETER_STRUCT(FVoxelMarkValidPageIndex_IndirectArgsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, DeferredScatterCounter)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutIndirectArgsBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_MARKVALID_INDIRECTARG"), 1);
	}
};

class FVoxelMarkValidPageIndex_ScatterCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVoxelMarkValidPageIndex_ScatterCS);
	SHADER_USE_PARAMETER_STRUCT(FVoxelMarkValidPageIndex_ScatterCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER(Buffer<int>,		IndirectBufferArgs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint4>,	PageIndexResolutionAndOffsetBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>,	DeferredScatterCounter)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint2>,  DeferredScatterBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutValidPageIndexBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_MARKVALID_SCATTER"), 1);
	}
};

class FVoxelMarkValidPageIndexCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVoxelMarkValidPageIndexCS);
	SHADER_USE_PARAMETER_STRUCT(FVoxelMarkValidPageIndexCS, FGlobalShader);

	class FGPUDriven : SHADER_PERMUTATION_INT("PERMUTATION_GPU_DRIVEN", 2);
	using FPermutationDomain = TShaderPermutationDomain<FGPUDriven>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntVector, CPU_PageIndexResolution)
		SHADER_PARAMETER(FVector, CPU_MinAABB)
		SHADER_PARAMETER(uint32, MaxClusterCount)
		SHADER_PARAMETER(FVector, CPU_MaxAABB)
		SHADER_PARAMETER(uint32, CPU_PageIndexOffset)
		SHADER_PARAMETER(uint32, MacroGroupId)
		SHADER_PARAMETER_SRV(Buffer, ClusterAABBsBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, MacroGroupAABBBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, PageIndexResolutionAndOffsetBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(Buffer<uint>, OutValidPageIndexBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_MARKVALID"), 1);
	}
};

class FVoxelAllocateVoxelPageCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVoxelAllocateVoxelPageCS);
	SHADER_USE_PARAMETER_STRUCT(FVoxelAllocateVoxelPageCS, FGlobalShader);

	class FGPUDriven : SHADER_PERMUTATION_INT("PERMUTATION_GPU_DRIVEN", 2);
	using FPermutationDomain = TShaderPermutationDomain<FGPUDriven>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntVector, CPU_PageIndexResolution)
		SHADER_PARAMETER(uint32, MacroGroupId)
		SHADER_PARAMETER(uint32, PageCount)
		SHADER_PARAMETER(uint32, CPU_PageIndexCount)
		SHADER_PARAMETER(uint32, CPU_PageIndexOffset)		
		SHADER_PARAMETER_RDG_BUFFER(Buffer, IndirectBufferArgs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, PageIndexResolutionAndOffsetBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, PageIndexGlobalCounter)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, PageIndexBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, PageToPageIndexBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, PageIndexCoordBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_ALLOCATE"), 1);
	}
};

class FVoxelAddNodeDescCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVoxelAddNodeDescCS);
	SHADER_USE_PARAMETER_STRUCT(FVoxelAddNodeDescCS, FGlobalShader);

	class FGPUDriven : SHADER_PERMUTATION_INT("PERMUTATION_GPU_DRIVEN", 2);
	using FPermutationDomain = TShaderPermutationDomain<FGPUDriven>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector, CPU_MinAABB)
		SHADER_PARAMETER(uint32, CPU_PageIndexOffset)
		SHADER_PARAMETER(FVector, CPU_MaxAABB)
		SHADER_PARAMETER(uint32, MacroGroupId)
		SHADER_PARAMETER(FIntVector, CPU_PageIndexResolution)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, MacroGroupAABBBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, PageIndexResolutionAndOffsetBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer, OutNodeDescBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_ADDDESC"), 1);
	}
};

class FVoxelAddIndirectBufferCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVoxelAddIndirectBufferCS);
	SHADER_USE_PARAMETER_STRUCT(FVoxelAddIndirectBufferCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, MacroGroupId)
		SHADER_PARAMETER(uint32, IndirectGroupSize)
		SHADER_PARAMETER(uint32, PageResolution)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, OutPageIndexGlobalCounter)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, OutIndirectArgsBuffer)
		END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_ADDINDIRECTBUFFER"), 1);
	}
};


class FVoxelIndPageClearBufferGenCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVoxelIndPageClearBufferGenCS);
	SHADER_USE_PARAMETER_STRUCT(FVoxelIndPageClearBufferGenCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, PageIndexGlobalCounter)
		SHADER_PARAMETER_RDG_BUFFER_UAV(Buffer, OutIndirectArgsBuffer)
		SHADER_PARAMETER(uint32, PageResolution)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_INDPAGECLEARBUFFERGEN"), 1);
	}
};

class FVoxelIndPageClearCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVoxelIndPageClearCS);
	SHADER_USE_PARAMETER_STRUCT(FVoxelIndPageClearCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT(FVirtualVoxelCommonParameters, VirtualVoxelParams)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(Texture3D, OutPageTexture)
		SHADER_PARAMETER_RDG_BUFFER(Buffer, IndirectDispatchBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_INDPAGECLEAR"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FVoxelMarkValidPageIndex_PrepareCS, "/Engine/Private/HairStrands/HairStrandsVoxelPageAllocation.usf", "MarkValid_PrepareCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FVoxelMarkValidPageIndex_IndirectArgsCS, "/Engine/Private/HairStrands/HairStrandsVoxelPageAllocation.usf", "MarkValid_BuildIndirectArgCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FVoxelMarkValidPageIndex_ScatterCS, "/Engine/Private/HairStrands/HairStrandsVoxelPageAllocation.usf", "MarkValid_ScatterCS", SF_Compute);

IMPLEMENT_GLOBAL_SHADER(FVoxelAllocatePageIndexCS, "/Engine/Private/HairStrands/HairStrandsVoxelPageAllocation.usf", "AllocatePageIndex", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FVoxelMarkValidPageIndexCS, "/Engine/Private/HairStrands/HairStrandsVoxelPageAllocation.usf", "MarkValidCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FVoxelAllocateVoxelPageCS, "/Engine/Private/HairStrands/HairStrandsVoxelPageAllocation.usf", "AllocateCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FVoxelAddNodeDescCS, "/Engine/Private/HairStrands/HairStrandsVoxelPageAllocation.usf", "AddDescCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FVoxelAddIndirectBufferCS, "/Engine/Private/HairStrands/HairStrandsVoxelPageAllocation.usf", "AddIndirectBufferCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FVoxelIndPageClearBufferGenCS, "/Engine/Private/HairStrands/HairStrandsVoxelPageAllocation.usf", "VoxelIndPageClearBufferGenCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FVoxelIndPageClearCS, "/Engine/Private/HairStrands/HairStrandsVoxelPageAllocation.usf", "VoxelIndPageClearCS", SF_Compute);

inline FIntVector CeilToInt(const FVector& V)
{
	return FIntVector(FMath::CeilToInt(V.X), FMath::CeilToInt(V.Y), FMath::CeilToInt(V.Z));
}

static void AddAllocateVoxelPagesPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FHairStrandsMacroGroupDatas& MacroGroups,
	const FIntVector PageCountResolution,
	const uint32 PageCount,
	const float VoxelWorldSize,
	const uint32 PageResolution,
	const FIntVector PageTextureResolution,
	const uint32 IndirectDispatchGroupSize,
	uint32& OutTotalPageIndexCount,
	FRDGBufferRef& OutPageIndexBuffer,
	FRDGBufferRef& OutPageToPageIndexBuffer,
	FRDGBufferRef& OutPageIndexCoordBuffer,
	FRDGBufferRef& OutNodeDescBuffer,
	FRDGBufferRef& OutIndirectArgsBuffer,
	FRDGBufferRef& OutPageIndexGlobalCounter, 
	FRDGBufferRef& OutVoxelizationViewInfoBuffer)
{
	const uint32 GroupSize = 32;
	const bool bIsGPUDriven = GHairVirtualVoxelGPUDriven > 0;
	const uint32 MacroGroupCount = MacroGroups.Datas.Num();
	if (MacroGroupCount == 0)
		return;

	struct FCPUMacroGroupAllocation
	{
		FVector		MinAABB;
		FVector		MaxAABB;
		FIntVector	PageIndexResolution;
		uint32		PageIndexCount;
		uint32		PageIndexOffset;
		uint32		MacroGroupId;
	};
	const float PageWorldSize = PageResolution * VoxelWorldSize;

	OutTotalPageIndexCount = 0;
	TArray<FCPUMacroGroupAllocation> CPUAllocationDescs;	
	for (FHairStrandsMacroGroupData& MacroGroup : MacroGroups.Datas)
	{
		// Snap the max AABB to the voxel size
		// Scale the bounding box in place of proper GPU driven AABB for now
		const float Scale = FMath::Clamp(GHairVoxelizationAABBScale, 0.01f, 10.0f);
		const FVector BoxCenter = MacroGroup.Bounds.GetBox().GetCenter();
		FVector MinAABB = (MacroGroup.Bounds.GetBox().Min - BoxCenter) * Scale + BoxCenter;
		FVector MaxAABB = (MacroGroup.Bounds.GetBox().Max - BoxCenter) * Scale + BoxCenter;

		// Allocate enough pages to cover the AABB, where page (0,0,0) origin sit on MinAABB.
		FVector MacroGroupSize = MaxAABB - MinAABB;
		const FIntVector PageIndexResolution = CeilToInt(MacroGroupSize / PageWorldSize);
		MacroGroupSize = FVector(PageIndexResolution) * PageWorldSize;
		MaxAABB = MacroGroupSize + MinAABB;

		FCPUMacroGroupAllocation& Out = CPUAllocationDescs.AddDefaulted_GetRef();
		Out.MacroGroupId = MacroGroup.MacroGroupId;
		Out.MinAABB = MinAABB; // >> these should actually be computed on the GPU ... 
		Out.MaxAABB = MaxAABB; // >> these should actually be computed on the GPU ... 
		Out.PageIndexResolution = PageIndexResolution;
		Out.PageIndexCount = Out.PageIndexResolution.X * Out.PageIndexResolution.Y * Out.PageIndexResolution.Z;
		Out.PageIndexOffset = OutTotalPageIndexCount;

		OutTotalPageIndexCount += Out.PageIndexCount;

		MacroGroup.VirtualVoxelNodeDesc.WorldMinAABB = Out.MinAABB;
		MacroGroup.VirtualVoxelNodeDesc.WorldMaxAABB = Out.MaxAABB;
		MacroGroup.VirtualVoxelNodeDesc.PageIndexResolution = Out.PageIndexResolution;
	}

	// Over-allocation (upper bound)
	if (bIsGPUDriven)
	{
		OutTotalPageIndexCount = GHairVirtualVoxelGPUDrivenMaxPageIndexRes * GHairVirtualVoxelGPUDrivenMaxPageIndexRes * GHairVirtualVoxelGPUDrivenMaxPageIndexRes;
	}
	check(OutTotalPageIndexCount > 0);
	
	FRDGBufferRef PageIndexBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), OutTotalPageIndexCount), TEXT("PageIndexBuffer"));
	FRDGBufferRef PageIndexCoordBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), OutTotalPageIndexCount), TEXT("PageIndexCoordBuffer"));
	FRDGBufferRef PageIndexGlobalCounter = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 2), TEXT("PageIndexGlobalCounter"));
	FRDGBufferRef NodeDescBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FPackedVirtualVoxelNodeDesc), MacroGroupCount), TEXT("VirtualVoxelNodeDescBuffer"));
	FRDGBufferRef IndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(MacroGroupCount), TEXT("VirtualVoxelIndirectArgsBuffer"));

	const uint32 TotalPageCount = PageCountResolution.X * PageCountResolution.Y * PageCountResolution.Z;
	FRDGBufferRef PageToPageIndexBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), TotalPageCount), TEXT("PageToPageIndexBuffer"));

	FRDGBufferUAVRef PageIndexBufferUAV = GraphBuilder.CreateUAV(PageIndexBuffer, PF_R32_UINT);
	FRDGBufferUAVRef PageIndexGlobalCounterUAV = GraphBuilder.CreateUAV(PageIndexGlobalCounter, PF_R32_UINT);
	
	// Stored FVoxelizationViewInfo structs
	// See HairStrandsVoxelPageCommonStruct.ush for more details
	FRDGBufferRef VoxelizationViewInfoBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(24 * sizeof(float), MacroGroupCount), TEXT("VoxelizationViewInfo"));
	FRDGBufferRef PageIndexResolutionBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(MacroGroupCount * 4 * sizeof(uint32), OutTotalPageIndexCount), TEXT("PageIndexResolutionBuffer"));
	FRDGBufferRef MacroGroupAABB = GraphBuilder.RegisterExternalBuffer(MacroGroups.MacroGroupResources.MacroGroupAABBsBuffer, TEXT("HairInstanceGroupAABBs"));
	FRDGBufferRef PageIndexAllocationIndirectBufferArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(MacroGroupCount), TEXT("PageIndexAllocationIndirectBufferArgs"));

	AddClearUAVPass(GraphBuilder, PageIndexBufferUAV, 0u);
	AddClearUAVPass(GraphBuilder, PageIndexGlobalCounterUAV, 0u);

	// Allocate page index for all instance group
//	if (bIsGPUDriven)
	{		

		FVoxelAllocatePageIndexCS::FParameters* Parameters = GraphBuilder.AllocParameters<FVoxelAllocatePageIndexCS::FParameters>();
		Parameters->PageWorldSize = PageWorldSize;
		Parameters->TotalPageIndexCount = OutTotalPageIndexCount;
		Parameters->PageResolution = PageResolution;
		Parameters->MacroGroupCount = MacroGroupCount;
		Parameters->MacroGroupAABBBuffer = GraphBuilder.CreateUAV(MacroGroupAABB, PF_R32_SINT);
		Parameters->IndirectDispatchGroupSize = GroupSize; // This is the GroupSize used for FVoxelAllocateVoxelPageCS
		Parameters->OutPageIndexResolutionAndOffsetBuffer = GraphBuilder.CreateUAV(PageIndexResolutionBuffer, PF_R32G32B32A32_UINT);
		Parameters->OutVoxelizationViewInfoBuffer = GraphBuilder.CreateUAV(VoxelizationViewInfoBuffer);
		Parameters->OutPageIndexAllocationIndirectBufferArgs = GraphBuilder.CreateUAV(PageIndexAllocationIndirectBufferArgs);
		const bool bUseCPUData = GHairVirtualVoxelGPUDriven == 2;
		if (bUseCPUData)
		{
			Parameters->CPU_bUseCPUData = bUseCPUData ? 1 : 0;
			Parameters->CPU_MinAABB = CPUAllocationDescs.Num() > 0 ? CPUAllocationDescs[0].MinAABB : FVector::ZeroVector;
			Parameters->CPU_MaxAABB = CPUAllocationDescs.Num() > 0 ? CPUAllocationDescs[0].MaxAABB : FVector::ZeroVector;
			Parameters->CPU_PageIndexResolution = CPUAllocationDescs.Num() > 0 ? CPUAllocationDescs[0].PageIndexResolution : FIntVector(0, 0, 0);
		}

		// Currently support only 32 instance group at max
		check(Parameters->MacroGroupCount < 32);
		TShaderMapRef<FVoxelAllocatePageIndexCS> ComputeShader(View.ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("HairStrandsAllocatePageIndex"),
			ComputeShader,
			Parameters,
			FIntVector(1,1,1));
	}
	FRDGBufferSRVRef PageIndexResolutionAndOffsetBufferSRV = GraphBuilder.CreateSRV(PageIndexResolutionBuffer, PF_R32G32B32A32_UINT);

	uint32 TotalClusterCount = 0;
	for (uint32 MacroGroupIt = 0; MacroGroupIt < MacroGroupCount; ++MacroGroupIt)
	{
		const FHairStrandsMacroGroupData& MacroGroup = MacroGroups.Datas[MacroGroupIt];
		for (const FHairStrandsMacroGroupData::PrimitiveGroup& PrimitiveGroup : MacroGroup.PrimitivesGroups)
		{
			const FHairStrandsPrimitiveResources& Resources = GetHairStandsPrimitiveResources(PrimitiveGroup.ResourceId);
			check(PrimitiveGroup.GroupIndex < uint32(Resources.Groups.Num()));
			const FHairStrandsPrimitiveResources::FHairGroup& GroupResources = Resources.Groups[PrimitiveGroup.GroupIndex];
			TotalClusterCount += GroupResources.ClusterCount;
		}
	}

	// Mark valid page index
	for (uint32 MacroGroupIt=0; MacroGroupIt <MacroGroupCount;++MacroGroupIt)
	{
		DECLARE_GPU_STAT(HairStrandsAllocateMacroGroup);
		SCOPED_DRAW_EVENT(GraphBuilder.RHICmdList, HairStrandsAllocateMacroGroup);
		SCOPED_GPU_STAT(GraphBuilder.RHICmdList, HairStrandsAllocateMacroGroup);

		const FHairStrandsMacroGroupData& MacroGroup = MacroGroups.Datas[MacroGroupIt];
		FCPUMacroGroupAllocation& CPUAllocationDesc = CPUAllocationDescs[MacroGroupIt];

		const bool bUseIndirectScatter = GHairVirtualVoxelUseIndirectScatterPageAllocation>0 && bIsGPUDriven;
		if (bUseIndirectScatter)
		{			
			const uint32 AverageWorkItemPerCluster = 8; // Arbitrary/Guess number
			const uint32 MaxAllocationCount = TotalClusterCount * AverageWorkItemPerCluster;
			FRDGBufferRef ScatterCounter = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("PageScatterCounter"));
			FRDGBufferRef ScatterBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(2 * sizeof(uint32), MaxAllocationCount), TEXT("PageScatterBuffer"));

			FRDGBufferUAVRef ScatterCounterUAV = GraphBuilder.CreateUAV(ScatterCounter, PF_R32_UINT);
			FRDGBufferUAVRef ScatterBufferUAV  = GraphBuilder.CreateUAV(ScatterBuffer, PF_R32G32_UINT);

			AddClearUAVPass(GraphBuilder, ScatterCounterUAV, 0);

			// Prepare
			for (const FHairStrandsMacroGroupData::PrimitiveGroup& PrimitiveGroup : MacroGroup.PrimitivesGroups)
			{
				const FHairStrandsPrimitiveResources& Resources = GetHairStandsPrimitiveResources(PrimitiveGroup.ResourceId);
				check(PrimitiveGroup.GroupIndex < uint32(Resources.Groups.Num()));
				const FHairStrandsPrimitiveResources::FHairGroup& GroupResources = Resources.Groups[PrimitiveGroup.GroupIndex];

				FVoxelMarkValidPageIndex_PrepareCS::FParameters* Parameters = GraphBuilder.AllocParameters<FVoxelMarkValidPageIndex_PrepareCS::FParameters>();
				Parameters->MaxClusterCount			= GroupResources.ClusterCount;
				Parameters->MacroGroupId			= MacroGroup.MacroGroupId;
				Parameters->MaxScatterAllocationCount= MaxAllocationCount;

				Parameters->ClusterAABBsBuffer		= GroupResources.ClusterAABBBuffer->SRV;
				Parameters->MacroGroupAABBBuffer	= GraphBuilder.CreateSRV(MacroGroupAABB, PF_R32_SINT);
				Parameters->PageIndexResolutionAndOffsetBuffer = PageIndexResolutionAndOffsetBufferSRV;

				Parameters->OutDeferredScatterCounter	= ScatterCounterUAV;
				Parameters->OutDeferredScatterBuffer	= ScatterBufferUAV;
				Parameters->OutValidPageIndexBuffer		= PageIndexBufferUAV;

				FIntVector DispatchCount((GroupResources.ClusterCount + GroupSize - 1) / GroupSize, 1, 1);
				check(DispatchCount.X < 65535);
				TShaderMapRef<FVoxelMarkValidPageIndex_PrepareCS> ComputeShader(View.ShaderMap);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("HairStrandsMarkValidPageIndex_Prepare"),
					ComputeShader,
					Parameters,
					DispatchCount);
			}
			
			FRDGBufferSRVRef ScatterCounterSRV = GraphBuilder.CreateSRV(ScatterCounter, PF_R32_UINT);

			// Build indirect buffer args
			FRDGBufferRef ScatterIndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("PageScatterIndirectArgs"));
			{
				check(MacroGroup.MacroGroupId < MacroGroupCount);

				FVoxelMarkValidPageIndex_IndirectArgsCS::FParameters* Parameters = GraphBuilder.AllocParameters<FVoxelMarkValidPageIndex_IndirectArgsCS::FParameters>();
				Parameters->DeferredScatterCounter = ScatterCounterSRV;
				Parameters->OutIndirectArgsBuffer = GraphBuilder.CreateUAV(ScatterIndirectArgsBuffer);

				const FIntVector DispatchCount(1, 1, 1);
				TShaderMapRef<FVoxelMarkValidPageIndex_IndirectArgsCS> ComputeShader(View.ShaderMap);
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("HairStrandsMarkValidPageIndex_IndirectArgs"), ComputeShader, Parameters, DispatchCount);
			}

			// Scatter
			{
				FRDGBufferSRVRef ScatterBufferSRV = GraphBuilder.CreateSRV(ScatterBuffer, PF_R32G32_UINT);
				check(MacroGroup.MacroGroupId < MacroGroupCount);

				FVoxelMarkValidPageIndex_ScatterCS::FParameters* Parameters = GraphBuilder.AllocParameters<FVoxelMarkValidPageIndex_ScatterCS::FParameters>();
				Parameters->IndirectBufferArgs = ScatterIndirectArgsBuffer;
				Parameters->PageIndexResolutionAndOffsetBuffer = PageIndexResolutionAndOffsetBufferSRV;
				Parameters->DeferredScatterCounter = ScatterCounterSRV;
				Parameters->DeferredScatterBuffer = ScatterBufferSRV;
				Parameters->OutValidPageIndexBuffer = PageIndexBufferUAV;

				TShaderMapRef<FVoxelMarkValidPageIndex_ScatterCS> ComputeShader(View.ShaderMap);
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("HairStrandsMarkValidPageIndex_Scatter"), ComputeShader, Parameters, ScatterIndirectArgsBuffer, 0);
			}
		}
		else
		{		
			for (const FHairStrandsMacroGroupData::PrimitiveGroup& PrimitiveGroup : MacroGroup.PrimitivesGroups)
			{
				const FHairStrandsPrimitiveResources& Resources = GetHairStandsPrimitiveResources(PrimitiveGroup.ResourceId);
				check(PrimitiveGroup.GroupIndex < uint32(Resources.Groups.Num()));
				const FHairStrandsPrimitiveResources::FHairGroup& GroupResources = Resources.Groups[PrimitiveGroup.GroupIndex];

				FVoxelMarkValidPageIndexCS::FParameters* Parameters = GraphBuilder.AllocParameters<FVoxelMarkValidPageIndexCS::FParameters>();
				Parameters->MacroGroupId = MacroGroup.MacroGroupId;
				Parameters->MaxClusterCount = GroupResources.ClusterCount;
				Parameters->CPU_PageIndexResolution = CPUAllocationDesc.PageIndexResolution;
				Parameters->CPU_PageIndexOffset = CPUAllocationDesc.PageIndexOffset;
				Parameters->CPU_MinAABB = CPUAllocationDesc.MinAABB;
				Parameters->CPU_MaxAABB = CPUAllocationDesc.MaxAABB;
				Parameters->ClusterAABBsBuffer = GroupResources.ClusterAABBBuffer->SRV;
				Parameters->OutValidPageIndexBuffer = PageIndexBufferUAV;

				if (bIsGPUDriven)
				{
					Parameters->MacroGroupAABBBuffer = GraphBuilder.CreateSRV(MacroGroupAABB, PF_R32_SINT);
					Parameters->PageIndexResolutionAndOffsetBuffer = PageIndexResolutionAndOffsetBufferSRV;
				}

				FVoxelMarkValidPageIndexCS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FVoxelMarkValidPageIndexCS::FGPUDriven>(bIsGPUDriven ? 1 : 0);

				FIntVector DispatchCount((GroupResources.ClusterCount + GroupSize - 1) / GroupSize, 1, 1);
				check(DispatchCount.X < 65535);
				TShaderMapRef<FVoxelMarkValidPageIndexCS> ComputeShader(View.ShaderMap, PermutationVector);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("HairStrandsMarkValidPageIndex"),
					ComputeShader,
					Parameters,
					DispatchCount);
			}
		}

		// Fill in hair-macro-group information.
		// Note: This need to happen before the allocation as we copy the index global count. This global index is 
		// used as an offset, and thus refers to the previous pass
		{
			check(MacroGroup.MacroGroupId < MacroGroupCount);

			FVoxelAddNodeDescCS::FParameters* Parameters = GraphBuilder.AllocParameters<FVoxelAddNodeDescCS::FParameters>();
			Parameters->MacroGroupId = MacroGroup.MacroGroupId;
			Parameters->CPU_MinAABB = CPUAllocationDesc.MinAABB;
			Parameters->CPU_MaxAABB = CPUAllocationDesc.MaxAABB;
			Parameters->CPU_PageIndexResolution = CPUAllocationDesc.PageIndexResolution;
			Parameters->CPU_PageIndexOffset = CPUAllocationDesc.PageIndexOffset;
			Parameters->OutNodeDescBuffer = GraphBuilder.CreateUAV(NodeDescBuffer);

			if (bIsGPUDriven)
			{
				Parameters->MacroGroupAABBBuffer = GraphBuilder.CreateSRV(MacroGroupAABB, PF_R32_SINT);
				Parameters->PageIndexResolutionAndOffsetBuffer = PageIndexResolutionAndOffsetBufferSRV;
			}

			FVoxelAddNodeDescCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FVoxelAddNodeDescCS::FGPUDriven>(bIsGPUDriven ? 1 : 0);

			const FIntVector DispatchCount(1, 1, 1);
			TShaderMapRef<FVoxelAddNodeDescCS> ComputeShader(View.ShaderMap, PermutationVector);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("HairStrandsAddNodeDesc"), ComputeShader, Parameters, DispatchCount);
		}

		// Allocate pages
		{
			FVoxelAllocateVoxelPageCS::FParameters* Parameters = GraphBuilder.AllocParameters<FVoxelAllocateVoxelPageCS::FParameters>();
			Parameters->MacroGroupId = MacroGroup.MacroGroupId;
			Parameters->PageCount = PageCount;
			Parameters->CPU_PageIndexCount = CPUAllocationDesc.PageIndexCount;
			Parameters->CPU_PageIndexResolution = CPUAllocationDesc.PageIndexResolution;
			Parameters->CPU_PageIndexOffset = CPUAllocationDesc.PageIndexOffset;
			Parameters->PageIndexGlobalCounter = PageIndexGlobalCounterUAV;
			Parameters->PageIndexBuffer = PageIndexBufferUAV;
			Parameters->PageToPageIndexBuffer = GraphBuilder.CreateUAV(PageToPageIndexBuffer, PF_R32_UINT);
			Parameters->PageIndexCoordBuffer = GraphBuilder.CreateUAV(PageIndexCoordBuffer, PF_R8G8B8A8_UINT);

			FVoxelAllocateVoxelPageCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FVoxelAllocateVoxelPageCS::FGPUDriven>(bIsGPUDriven ? 1 : 0);
			TShaderMapRef<FVoxelAllocateVoxelPageCS> ComputeShader(View.ShaderMap, PermutationVector);

			if (bIsGPUDriven)
			{
				Parameters->PageIndexResolutionAndOffsetBuffer = PageIndexResolutionAndOffsetBufferSRV;
				Parameters->IndirectBufferArgs = PageIndexAllocationIndirectBufferArgs;

				const uint32 ArgsOffset = sizeof(uint32) * 3 * MacroGroup.MacroGroupId;

				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("HairStrandsAllocateVoxelPage"), ComputeShader, Parameters, 
					PageIndexAllocationIndirectBufferArgs,
					ArgsOffset);
			}
			else
			{
				const FIntVector DispatchCount((CPUAllocationDesc.PageIndexCount + GroupSize - 1) / GroupSize, 1, 1);
				check(DispatchCount.X < 65535);
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("HairStrandsAllocateVoxelPage"), ComputeShader, Parameters, DispatchCount);
			}
		}

		// Prepare indirect dispatch buffers
		{
			check(MacroGroup.MacroGroupId < MacroGroupCount);

			FVoxelAddIndirectBufferCS::FParameters* Parameters = GraphBuilder.AllocParameters<FVoxelAddIndirectBufferCS::FParameters>();
			Parameters->MacroGroupId = MacroGroup.MacroGroupId;
			Parameters->PageResolution = PageResolution;
			Parameters->IndirectGroupSize = IndirectDispatchGroupSize;
			Parameters->OutPageIndexGlobalCounter = GraphBuilder.CreateUAV(PageIndexGlobalCounter, PF_R32_UINT);
			Parameters->OutIndirectArgsBuffer = GraphBuilder.CreateUAV(IndirectArgsBuffer);

			const FIntVector DispatchCount(1, 1, 1);
			TShaderMapRef<FVoxelAddIndirectBufferCS> ComputeShader(View.ShaderMap);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("HairStrandsBuildVoxelIndirectArgs"), ComputeShader, Parameters, DispatchCount);
		}
	}

	OutPageIndexBuffer = PageIndexBuffer;
	OutPageToPageIndexBuffer = PageToPageIndexBuffer;
	OutPageIndexCoordBuffer = PageIndexCoordBuffer;
	OutNodeDescBuffer = NodeDescBuffer;
	OutIndirectArgsBuffer = IndirectArgsBuffer;
	OutPageIndexGlobalCounter = PageIndexGlobalCounter;
	OutVoxelizationViewInfoBuffer = VoxelizationViewInfoBuffer;
}

FVirtualVoxelResources AllocateVirtualVoxelResources(
	FRHICommandListImmediate& RHICmdList,
	const FViewInfo& View,
	FHairStrandsMacroGroupDatas& MacroGroups, 
	TRefCountPtr<FPooledRDGBuffer>& PageToPageIndexBuffer)
{
	DECLARE_GPU_STAT(HairStrandsVoxelPageAllocation);
	SCOPED_DRAW_EVENT(RHICmdList, HairStrandsVoxelPageAllocation);
	SCOPED_GPU_STAT(RHICmdList, HairStrandsVoxelPageAllocation);

	FRDGBuilder GraphBuilder(RHICmdList);
	FRDGBufferRef OutPageIndexBuffer = nullptr;
	FRDGBufferRef OutPageToPageIndexBuffer = nullptr;
	FRDGBufferRef OutPageIndexCoordBuffer = nullptr;
	FRDGBufferRef OutNodeDescBuffer = nullptr;
	FRDGBufferRef OutIndirectArgsBuffer = nullptr;
	FRDGBufferRef OutPageIndexGlobalCounter = nullptr;
	FRDGBufferRef OutVoxelizationViewInfoBuffer = nullptr;

	FVirtualVoxelResources Out;

	Out.Parameters.Common.PageCountResolution		= FIntVector(GHairVirtualVoxel_PageCountPerDim, GHairVirtualVoxel_PageCountPerDim, GHairVirtualVoxel_PageCountPerDim);
	Out.Parameters.Common.PageCount					= Out.Parameters.Common.PageCountResolution.X * Out.Parameters.Common.PageCountResolution.Y * Out.Parameters.Common.PageCountResolution.Z;
	Out.Parameters.Common.VoxelWorldSize			= FMath::Clamp(GHairVirtualVoxel_VoxelWorldSize, 0.01f, 10.f);
	Out.Parameters.Common.PageResolution			= FMath::RoundUpToPowerOfTwo(FMath::Clamp(GHairVirtualVoxel_PageResolution, 2, 256));
	Out.Parameters.Common.PageTextureResolution		= Out.Parameters.Common.PageCountResolution * Out.Parameters.Common.PageResolution;
	Out.Parameters.Common.DensityScale				= GetHairStrandsVoxelizationDensityScale();
	Out.Parameters.Common.DepthBiasScale			= GetHairStrandsVoxelizationDepthBiasScale();
	Out.Parameters.Common.SteppingScale				= FMath::Clamp(GHairStransVoxelRaymarchingSteppingScale, 1.f, 10.f);
	Out.Parameters.Common.NodeDescCount				= MacroGroups.Datas.Num();
	Out.Parameters.Common.IndirectDispatchGroupSize = 64;
	
	Out.Parameters.Common.HairCoveragePixelRadiusAtDepth1	= ComputeMinStrandRadiusAtDepth1(FIntPoint(View.ViewRect.Width(), View.ViewRect.Height()), View.FOV, 1/*SampleCount*/, 1/*RasterizationScale*/).Primary;
	Out.Parameters.Common.HairCoverageLUT					= GetHairLUT(RHICmdList, View).Textures[HairLUTType_Coverage]->GetRenderTargetItem().TargetableTexture;
	Out.Parameters.Common.HairCoverageSampler				= TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	AddAllocateVoxelPagesPass(
		GraphBuilder, 
		View, 
		MacroGroups,
		Out.Parameters.Common.PageCountResolution,
		Out.Parameters.Common.PageCount,
		Out.Parameters.Common.VoxelWorldSize,
		Out.Parameters.Common.PageResolution,
		Out.Parameters.Common.PageTextureResolution,
		Out.Parameters.Common.IndirectDispatchGroupSize,
		Out.Parameters.Common.PageIndexCount,
		OutPageIndexBuffer, 
		OutPageToPageIndexBuffer,
		OutPageIndexCoordBuffer,
		OutNodeDescBuffer,
		OutIndirectArgsBuffer,
		OutPageIndexGlobalCounter,
		OutVoxelizationViewInfoBuffer);

	if (OutPageIndexBuffer)
	{
		GraphBuilder.QueueBufferExtraction(OutPageIndexBuffer, &Out.PageIndexBuffer, FRDGResourceState::EAccess::Read, FRDGResourceState::EPipeline::Graphics);
	}

	if (OutPageToPageIndexBuffer)
	{
		GraphBuilder.QueueBufferExtraction(OutPageToPageIndexBuffer, &PageToPageIndexBuffer, FRDGResourceState::EAccess::Read, FRDGResourceState::EPipeline::Graphics);
	}

	if (OutPageIndexCoordBuffer)
	{
		GraphBuilder.QueueBufferExtraction(OutPageIndexCoordBuffer, &Out.PageIndexCoordBuffer, FRDGResourceState::EAccess::Read, FRDGResourceState::EPipeline::Compute);
	}

	if (OutNodeDescBuffer)
	{
		GraphBuilder.QueueBufferExtraction(OutNodeDescBuffer, &Out.NodeDescBuffer, FRDGResourceState::EAccess::Read, FRDGResourceState::EPipeline::Graphics);
	}

	if (OutIndirectArgsBuffer)
	{
		GraphBuilder.QueueBufferExtraction(OutIndirectArgsBuffer, &Out.IndirectArgsBuffer, FRDGResourceState::EAccess::Read, FRDGResourceState::EPipeline::Compute);
	}

	if (OutPageIndexGlobalCounter)
	{
		GraphBuilder.QueueBufferExtraction(OutPageIndexGlobalCounter, &Out.PageIndexGlobalCounter, FRDGResourceState::EAccess::Read, FRDGResourceState::EPipeline::Compute);
	}

	if (OutVoxelizationViewInfoBuffer)
	{
		GraphBuilder.QueueBufferExtraction(OutVoxelizationViewInfoBuffer, &Out.VoxelizationViewInfoBuffer, FRDGResourceState::EAccess::Read, FRDGResourceState::EPipeline::Compute);
	}

	GraphBuilder.Execute();

	if (Out.PageIndexBuffer)
	{
		Out.PageIndexBufferSRV = RHICreateShaderResourceView(Out.PageIndexBuffer->VertexBuffer, sizeof(uint32), PF_R32_UINT);
	}

	if (Out.PageIndexCoordBuffer)
	{
		Out.PageIndexCoordBufferSRV = RHICreateShaderResourceView(Out.PageIndexCoordBuffer->VertexBuffer, sizeof(uint32), PF_R8G8B8A8_UINT);
	}

	if (Out.NodeDescBuffer)
	{
		Out.NodeDescBufferSRV = RHICreateShaderResourceView(Out.NodeDescBuffer->StructuredBuffer);
	}

	{
		// Allocation should be conservative
		// TODO: do a partial clear with indirect call: we know how many texture page will be touched, so we know how much thread we need to launch to clear what is relevant
		check(FMath::IsPowerOfTwo(Out.Parameters.Common.PageResolution));
		const uint32 MipCount = FMath::Log2(Out.Parameters.Common.PageResolution) + 1;

		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::CreateVolumeDesc(
			Out.Parameters.Common.PageTextureResolution.X, 
			Out.Parameters.Common.PageTextureResolution.Y, 
			Out.Parameters.Common.PageTextureResolution.Z, 
			PF_R32_UINT, 
			FClearValueBinding::Black, 
			TexCreate_None, TexCreate_UAV | TexCreate_ShaderResource, 
			false, 
			MipCount));

		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, Out.PageTexture, TEXT("VoxelPageTexture"));
	}

	Out.Parameters.Common.PageIndexBuffer		= Out.PageIndexBufferSRV;
	Out.Parameters.Common.PageIndexCoordBuffer	= Out.PageIndexCoordBufferSRV;
	Out.Parameters.Common.NodeDescBuffer		= Out.NodeDescBufferSRV; 
	Out.Parameters.PageTexture					= Out.PageTexture->GetRenderTargetItem().ShaderResourceTexture;

	if (Out.PageIndexBufferSRV && Out.NodeDescBufferSRV)
	{
		Out.UniformBuffer = CreateUniformBufferImmediate(Out.Parameters, UniformBuffer_SingleFrame);
	}

	return Out;
}


static FRDGBufferRef IndirectVoxelPageClear(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& ViewInfo,
	FVirtualVoxelResources& VoxelResources)
{
	DECLARE_GPU_STAT(HairStrandsIndVoxelPageClear);
	SCOPED_DRAW_EVENT(GraphBuilder.RHICmdList, HairStrandsIndVoxelPageClear);
	SCOPED_GPU_STAT(GraphBuilder.RHICmdList, HairStrandsIndVoxelPageClear);

	FRDGBufferRef ClearIndArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("VirtualVoxelClearIndirectArgsBuffer"));

	// Generate the indirect buffer required to clear all voxel allocated linearly in the page volume texture, using the global counter.
	{
		FRDGBufferRef PageIndexGlobalCounter = GraphBuilder.RegisterExternalBuffer(VoxelResources.PageIndexGlobalCounter, TEXT("HairPageIndexGlobalCounter"));

		FVoxelIndPageClearBufferGenCS::FParameters* Parameters = GraphBuilder.AllocParameters<FVoxelIndPageClearBufferGenCS::FParameters>();
		Parameters->PageResolution = VoxelResources.Parameters.Common.PageResolution;
		Parameters->OutIndirectArgsBuffer = GraphBuilder.CreateUAV(ClearIndArgsBuffer);
		Parameters->PageIndexGlobalCounter = GraphBuilder.CreateSRV(PageIndexGlobalCounter, PF_R32_UINT);

		TShaderMapRef<FVoxelIndPageClearBufferGenCS> ComputeShader(ViewInfo.ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("HairStrandsVoxelGenIndBufferClearCS"),
			ComputeShader,
			Parameters,
			FIntVector(1,1,1));
	}

	// Now single dispatch to clear all the pages
	{
		FRDGTextureRef OutPageTexture = GraphBuilder.RegisterExternalTexture(VoxelResources.PageTexture, TEXT("HairVoxelPageTexture"));
		FRDGBufferRef IndirectDispatchArgsBuffer = GraphBuilder.RegisterExternalBuffer(VoxelResources.IndirectArgsBuffer, TEXT("HairVoxelIndirectDispatchArgs"));

		FVoxelIndPageClearCS::FParameters* Parameters = GraphBuilder.AllocParameters<FVoxelIndPageClearCS::FParameters>();
		Parameters->VirtualVoxelParams = VoxelResources.Parameters.Common;
		Parameters->OutPageTexture = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(OutPageTexture));
		Parameters->IndirectDispatchBuffer = ClearIndArgsBuffer;

		TShaderMapRef<FVoxelIndPageClearCS> ComputeShader(ViewInfo.ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("HairStrandsVoxelIndPageClearCS"),
			ComputeShader,
			Parameters,
			ClearIndArgsBuffer,
			0);
	}

	return ClearIndArgsBuffer;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

class FVoxelRasterComputeCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVoxelRasterComputeCS);
	SHADER_USE_PARAMETER_STRUCT(FVoxelRasterComputeCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT(FVirtualVoxelCommonParameters, VirtualVoxelParams)
		SHADER_PARAMETER(uint32, MacroGroupId)
		SHADER_PARAMETER(uint32, DispatchCountX)
		SHADER_PARAMETER(uint32, MaxRasterCount)
		SHADER_PARAMETER(uint32, FrameIdMod8)
		SHADER_PARAMETER(float,	  HairStrandsVF_Density)
		SHADER_PARAMETER(float,   HairStrandsVF_Radius)
		SHADER_PARAMETER(float,	  HairStrandsVF_Length)
		SHADER_PARAMETER(FVector, HairStrandsVF_PositionOffset)
		SHADER_PARAMETER(uint32,  HairStrandsVF_VertexCount)
		SHADER_PARAMETER(FMatrix, HairStrandsVF_LocalToWorldPrimitiveTransform)
		SHADER_PARAMETER_SRV(Buffer, HairStrandsVF_PositionBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, VoxelizationViewInfoBuffer)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D, OutPageTexture)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_RASTERCOMPUTE"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FVoxelRasterComputeCS, "/Engine/Private/HairStrands/HairStrandsVoxelRasterCompute.usf", "MainCS", SF_Compute);

static void AddVirtualVoxelizationComputeRasterPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo* ViewInfo,
	FVirtualVoxelResources& VoxelResources,
	FHairStrandsMacroGroupData& MacroGroup)
{
	const bool bIsGPUDriven = GHairVirtualVoxelGPUDriven > 0;
	if (!bIsGPUDriven)
		return;

	if (ViewInfo)
	{
		const FHairStrandsMacroGroupData::TPrimitiveInfos& PrimitiveSceneInfos = MacroGroup.PrimitivesInfos;

		FRDGBufferRef VoxelizationViewInfoBuffer = GraphBuilder.RegisterExternalBuffer(VoxelResources.VoxelizationViewInfoBuffer);
		FRDGBufferSRVRef VoxelizationViewInfoBufferSRV = GraphBuilder.CreateSRV(VoxelizationViewInfoBuffer);
		FRDGTextureRef PageTexture = GraphBuilder.RegisterExternalTexture(VoxelResources.PageTexture);
		FRDGTextureUAVRef PageTextureUAV = GraphBuilder.CreateUAV(PageTexture);

		const uint32 FrameIdMode8 = ViewInfo && ViewInfo->ViewState ? (ViewInfo->ViewState->GetFrameIndex() % 8) : 0;
		const uint32 GroupSize = 32;
		const uint32 DispatchCountX = 64;
		TShaderMapRef<FVoxelRasterComputeCS> ComputeShader(ViewInfo->ShaderMap);
		for (const FHairStrandsMacroGroupData::PrimitiveInfo& PrimitiveInfo : PrimitiveSceneInfos)
		{
			FVoxelRasterComputeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVoxelRasterComputeCS::FParameters>();
			PassParameters->MaxRasterCount = FMath::Clamp(GHairStrandsVoxelComputeRasterMaxVoxelCount, 1, 256);
			PassParameters->VirtualVoxelParams = VoxelResources.Parameters.Common;
			PassParameters->MacroGroupId = MacroGroup.MacroGroupId;
			PassParameters->VoxelizationViewInfoBuffer = VoxelizationViewInfoBufferSRV;
			PassParameters->DispatchCountX = DispatchCountX;
			PassParameters->OutPageTexture = PageTextureUAV;
			PassParameters->FrameIdMod8 = FrameIdMode8;

			check(PrimitiveInfo.MeshBatchAndRelevance.Mesh && PrimitiveInfo.MeshBatchAndRelevance.Mesh->Elements.Num() > 0);
			const FHairGroupPublicData* HairGroupPublicData = reinterpret_cast<const FHairGroupPublicData*>(PrimitiveInfo.MeshBatchAndRelevance.Mesh->Elements[0].VertexFactoryUserData);
			const FHairGroupPublicData::VertexFactoryInput& VFInput = HairGroupPublicData->VFInput;
			PassParameters->HairStrandsVF_PositionBuffer = VFInput.HairPositionBuffer;
			PassParameters->HairStrandsVF_PositionOffset = VFInput.HairPositionOffset;
			PassParameters->HairStrandsVF_VertexCount = VFInput.VertexCount;
			PassParameters->HairStrandsVF_Radius = VFInput.HairRadius;
			PassParameters->HairStrandsVF_Length = VFInput.HairLength;
			PassParameters->HairStrandsVF_Density = VFInput.HairDensity;
			PassParameters->HairStrandsVF_LocalToWorldPrimitiveTransform = VFInput.LocalToWorldTransform.ToMatrixWithScale();

			const uint32 DispatchCountY = FMath::CeilToInt(PassParameters->HairStrandsVF_VertexCount / float(GroupSize * DispatchCountX));
			const FIntVector DispatchCount(DispatchCountX, DispatchCountY, 1);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("HairStrandsVoxelComputeRaster"), ComputeShader, PassParameters, DispatchCount);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////

static void AddVirtualVoxelizationRasterPass(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo* ViewInfo,
	FVirtualVoxelResources& VoxelResources,
	FHairStrandsMacroGroupData& MacroGroup)
{
	const bool bIsGPUDriven = GHairVirtualVoxelGPUDriven > 0;
	const FHairStrandsMacroGroupData::TPrimitiveInfos& PrimitiveSceneInfo = MacroGroup.PrimitivesInfos;
	DECLARE_GPU_STAT(HairStrandsVoxelize);
	SCOPED_DRAW_EVENT(GraphBuilder.RHICmdList, HairStrandsVoxelize);
	SCOPED_GPU_STAT(GraphBuilder.RHICmdList, HairStrandsVoxelize);

	// Find the largest resolution and its dominant axis
	FIntPoint RasterResolution(0, 0);
	FVector	RasterProjectionSize = FVector::ZeroVector;
	FVector RasterDirection = FVector::ZeroVector;
	FVector RasterUp = FVector::ZeroVector;
	const FIntVector TotalVoxelResolution = MacroGroup.VirtualVoxelNodeDesc.PageIndexResolution * VoxelResources.Parameters.Common.PageResolution;
	{
		FIntVector ReorderIndex(0, 0, 0);

		const uint32 ResolutionXY = TotalVoxelResolution.X * TotalVoxelResolution.Y;
		const uint32 ResolutionXZ = TotalVoxelResolution.X * TotalVoxelResolution.Z;
		const uint32 ResolutionYZ = TotalVoxelResolution.Y * TotalVoxelResolution.Y;
		if (ResolutionXY >= ResolutionXZ && ResolutionXY >= ResolutionYZ)
		{
			RasterResolution	= FIntPoint(TotalVoxelResolution.X, TotalVoxelResolution.Y);
			RasterDirection		= FVector(0, 0, 1);			
			ReorderIndex		= FIntVector(0, 1, 2);
			RasterUp			= FVector(0, 1, 0);
		}
		else if (ResolutionXZ >= ResolutionXY && ResolutionXZ >= ResolutionYZ)
		{
			RasterResolution	= FIntPoint(TotalVoxelResolution.X, TotalVoxelResolution.Z);
			RasterDirection		= FVector(0, -1, 0);
			ReorderIndex		= FIntVector(0, 2, 1);
			RasterUp			= FVector(0, 0, 1);
		}
		else
		{
			RasterResolution	= FIntPoint(TotalVoxelResolution.Y, TotalVoxelResolution.Z);
			RasterDirection		= FVector(1, 0, 0);
			ReorderIndex		= FIntVector(1, 2, 0);
			RasterUp			= FVector(0, 0, 1);
		}

		FBox ProjRasterAABB;
		ProjRasterAABB.Min.X = MacroGroup.VirtualVoxelNodeDesc.WorldMinAABB[ReorderIndex[0]];
		ProjRasterAABB.Min.Y = MacroGroup.VirtualVoxelNodeDesc.WorldMinAABB[ReorderIndex[1]];
		ProjRasterAABB.Min.Z = MacroGroup.VirtualVoxelNodeDesc.WorldMinAABB[ReorderIndex[2]];

		ProjRasterAABB.Max.X = MacroGroup.VirtualVoxelNodeDesc.WorldMaxAABB[ReorderIndex[0]];
		ProjRasterAABB.Max.Y = MacroGroup.VirtualVoxelNodeDesc.WorldMaxAABB[ReorderIndex[1]];
		ProjRasterAABB.Max.Z = MacroGroup.VirtualVoxelNodeDesc.WorldMaxAABB[ReorderIndex[2]];

		RasterProjectionSize = ProjRasterAABB.GetSize();
	}

	if (bIsGPUDriven)
	{
		RasterResolution = GPUDrivenViewportResolution;
	}

	const FBox RasterAABB(MacroGroup.VirtualVoxelNodeDesc.WorldMinAABB, MacroGroup.VirtualVoxelNodeDesc.WorldMaxAABB);
	const FVector RasterAABBSize = RasterAABB.GetSize();
	const FVector RasterAABBCenter = RasterAABB.GetCenter();
	const FIntRect ViewportRect = FIntRect(0, 0, RasterResolution.X, RasterResolution.Y);

	const float RadiusAtDepth1 = GStrandHairVoxelizationRasterizationScale * VoxelResources.Parameters.Common.VoxelWorldSize * 0.5f;
	const bool bIsOrtho = true;
	const FVector4 HairRenderInfo = PackHairRenderInfo(RadiusAtDepth1, RadiusAtDepth1, RadiusAtDepth1, 1);
	const uint32 HairRenderInfoBits = PackHairRenderInfoBits(bIsOrtho, bIsGPUDriven);

	FMatrix WorldToClip;
	{
		FReversedZOrthoMatrix OrthoMatrix(0.5f * RasterProjectionSize.X, 0.5f * RasterProjectionSize.Y, 1.0f / RasterProjectionSize.Z, 0);
		FLookAtMatrix LookAt(RasterAABBCenter - RasterDirection * RasterProjectionSize.Z * 0.5f, RasterAABBCenter, RasterUp);
		WorldToClip = LookAt * OrthoMatrix;
		MacroGroup.VirtualVoxelNodeDesc.WorldToClip = WorldToClip;
	}

	FRDGBufferRef VoxelizationViewInfoBuffer = GraphBuilder.RegisterExternalBuffer(VoxelResources.VoxelizationViewInfoBuffer);
	FRDGTextureRef PageTexture = GraphBuilder.RegisterExternalTexture(VoxelResources.PageTexture);

	const bool bUseComputeRaster = GHairStrandsVoxelComputeRaster > 0;
	if (bIsGPUDriven && bUseComputeRaster)
	{	
		AddVirtualVoxelizationComputeRasterPass(GraphBuilder, ViewInfo, VoxelResources, MacroGroup);
		return;
	}

	FHairVoxelizationRasterPassParameters* PassParameters = GraphBuilder.AllocParameters<FHairVoxelizationRasterPassParameters>();
	PassParameters->VirtualVoxel = VoxelResources.Parameters.Common;
	PassParameters->WorldToClipMatrix = WorldToClip;
	PassParameters->VoxelMinAABB = MacroGroup.VirtualVoxelNodeDesc.WorldMinAABB;
	PassParameters->VoxelMaxAABB = MacroGroup.VirtualVoxelNodeDesc.WorldMaxAABB;
	PassParameters->VoxelResolution = TotalVoxelResolution; // i.e., the virtual resolution
	PassParameters->MacroGroupId = MacroGroup.MacroGroupId;
	PassParameters->ViewportResolution = RasterResolution;
	PassParameters->VoxelizationViewInfoBuffer = GraphBuilder.CreateSRV(VoxelizationViewInfoBuffer);
	PassParameters->DensityTexture = GraphBuilder.CreateUAV(PageTexture);

	// For debug purpose
	#if 0
	FRDGTextureRef DebugOutputTexture = GraphBuilder.CreateTexture(FPooledRenderTargetDesc::Create2DDesc(RasterResolution, PF_R32_UINT, FClearValueBinding::Black, TexCreate_None, TexCreate_RenderTargetable, false), TEXT("DummyTexture"));
	PassParameters->RenderTargets[0] = FRenderTargetBinding(DebugOutputTexture, ERenderTargetLoadAction::EClear);
	#endif

	AddHairVoxelizationRasterPass(GraphBuilder, Scene, ViewInfo, PrimitiveSceneInfo, ViewportRect, HairRenderInfo, HairRenderInfoBits, RasterDirection, PassParameters);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
class FVirtualVoxelGenerateMipCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVirtualVoxelGenerateMipCS);
	SHADER_USE_PARAMETER_STRUCT(FVirtualVoxelGenerateMipCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER(FIntVector, PageCountResolution)
		SHADER_PARAMETER(uint32, PageResolution)
		SHADER_PARAMETER(uint32, SourceMip)
		SHADER_PARAMETER(uint32, TargetMip)

		SHADER_PARAMETER_RDG_BUFFER(StructuredBuffer, IndirectDispatchArgs)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D, InDensityTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D, OutDensityTexture)

	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_MIP_VIRTUALVOXEL"), 1);
	}
};

class FVirtualVoxelIndirectArgMipCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVirtualVoxelIndirectArgMipCS);
	SHADER_USE_PARAMETER_STRUCT(FVirtualVoxelIndirectArgMipCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, PageResolution)
		SHADER_PARAMETER(uint32, TargetMipIndex)
		SHADER_PARAMETER(uint32, DispatchGroupSize)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, InIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, OutIndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_MIP_INDIRECTARGS"), 1);
	}
};

class FVirtualVoxelPatchPageIndexWithMipDataCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVirtualVoxelPatchPageIndexWithMipDataCS);
	SHADER_USE_PARAMETER_STRUCT(FVirtualVoxelPatchPageIndexWithMipDataCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntVector, PageCountResolution)
		SHADER_PARAMETER(uint32, PageResolution)
		SHADER_PARAMETER(uint32, bUpdatePageIndex)
		SHADER_PARAMETER(uint32, MipIt)

		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, DensityTexture)
		SHADER_PARAMETER_RDG_BUFFER(Buffer, IndirectDispatchArgs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, PageToPageIndexBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutPageIndexBuffer)

	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_UPDATE_PAGEINDEX"), 1);
	}
};


IMPLEMENT_GLOBAL_SHADER(FVirtualVoxelGenerateMipCS, "/Engine/Private/HairStrands/HairStrandsVoxelMip.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FVirtualVoxelIndirectArgMipCS, "/Engine/Private/HairStrands/HairStrandsVoxelMip.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FVirtualVoxelPatchPageIndexWithMipDataCS, "/Engine/Private/HairStrands/HairStrandsVoxelMip.usf", "MainCS", SF_Compute);


static void AddVirtualVoxelGenerateMipPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FHairStrandsMacroGroupDatas& MacroGroups,
	FRDGBufferRef IndirectArgsBuffer, 
	TRefCountPtr<FPooledRDGBuffer>& InPageToPageIndexBuffer)
{
	if (!MacroGroups.VirtualVoxelResources.IsValid())
		return;

	DECLARE_GPU_STAT(HairStrandsDensityMipGen);
	SCOPED_DRAW_EVENT(GraphBuilder.RHICmdList, HairStrandsDensityMipGen);
	SCOPED_GPU_STAT(GraphBuilder.RHICmdList, HairStrandsDensityMipGen);

	FVirtualVoxelResources& VoxelResources = MacroGroups.VirtualVoxelResources;

	const uint32 MipCount = VoxelResources.PageTexture->GetDesc().NumMips;
	FRDGTextureRef VoxelDensityTexture = GraphBuilder.RegisterExternalTexture(VoxelResources.PageTexture, TEXT("HairVirtualVoxelDensityTexture"));
	FRDGBufferRef PageIndexBuffer = GraphBuilder.RegisterExternalBuffer(VoxelResources.PageIndexBuffer, TEXT("HairVirtualVoxelDensityTexturePageIndex"));

	// Prepare indirect dispatch for all the pages this frame (allocated linearly in 3D DensityTexture)
	TArray<FRDGBufferRef> MipIndirectArgsBuffers;
	for (uint32 MipIt = 0; MipIt < MipCount - 1; ++MipIt)
	{
		const uint32 TargetMipIndex = MipIt + 1;
		const uint32 DispatchGroupSize = 64;
		FRDGBufferRef MipIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(), TEXT("VirtualVoxelMipIndirectArgsBuffer"));
		MipIndirectArgsBuffers.Add(MipIndirectArgs);

		FVirtualVoxelIndirectArgMipCS::FParameters* Parameters = GraphBuilder.AllocParameters<FVirtualVoxelIndirectArgMipCS::FParameters>();
		Parameters->PageResolution		= VoxelResources.Parameters.Common.PageResolution;
		Parameters->TargetMipIndex		= TargetMipIndex;
		Parameters->DispatchGroupSize	= DispatchGroupSize;
		Parameters->InIndirectArgs		= GraphBuilder.CreateSRV(IndirectArgsBuffer);
		Parameters->OutIndirectArgs		= GraphBuilder.CreateUAV(MipIndirectArgs);

		TShaderMapRef<FVirtualVoxelIndirectArgMipCS> ComputeShader(View.ShaderMap);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("HairStrandsBuildVoxelMipIndirectArgs"), ComputeShader, Parameters, FIntVector(1, 1, 1));
	}

	// Generate MIP level (in one go for all allocated pages)
	for (uint32 MipIt = 0; MipIt < MipCount - 1; ++MipIt)
	{
		const uint32 SourceMipIndex = MipIt;
		const uint32 TargetMipIndex = MipIt + 1;

		FVirtualVoxelGenerateMipCS::FParameters* Parameters = GraphBuilder.AllocParameters<FVirtualVoxelGenerateMipCS::FParameters>();
		Parameters->InDensityTexture = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(VoxelDensityTexture, MipIt));
		Parameters->OutDensityTexture = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(VoxelDensityTexture, MipIt + 1));
		Parameters->PageResolution = VoxelResources.Parameters.Common.PageResolution;
		Parameters->PageCountResolution = VoxelResources.Parameters.Common.PageCountResolution;
		Parameters->SourceMip = SourceMipIndex;
		Parameters->TargetMip = TargetMipIndex;
		Parameters->IndirectDispatchArgs = MipIndirectArgsBuffers[MipIt];

		TShaderMapRef<FVirtualVoxelGenerateMipCS> ComputeShader(View.ShaderMap);
		ClearUnusedGraphResources(ComputeShader, Parameters);
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("HairStrandsComputeVoxelMip"),
			Parameters,
			ERDGPassFlags::Compute | ERDGPassFlags::GenerateMips,
			[Parameters, ComputeShader](FRHICommandList& RHICmdList)
		{
			FComputeShaderUtils::DispatchIndirect(RHICmdList, ComputeShader, *Parameters, Parameters->IndirectDispatchArgs->GetIndirectRHICallBuffer(), 0);
		});
	}

	// Patch the page index buffer with page whose voxels are empty after the voxelization is done
	FRDGBufferRef PageToPageIndexBuffer = GraphBuilder.RegisterExternalBuffer(InPageToPageIndexBuffer, TEXT("PageToPageIndexBuffer"));
	FRDGBufferSRVRef PageToPageIndexBufferSRV = GraphBuilder.CreateSRV(PageToPageIndexBuffer, PF_R32_UINT);
	FRDGBufferUAVRef PageIndexBufferUAV = GraphBuilder.CreateUAV(PageIndexBuffer, PF_R32_UINT);
	const bool bPatchEmptyPage = GHairVirtualVoxelInvalidEmptyPageIndex > 0;
	if (bPatchEmptyPage)
	{
		const uint32 LastMipIt = MipCount - 1;
		FVirtualVoxelPatchPageIndexWithMipDataCS::FParameters* Parameters = GraphBuilder.AllocParameters<FVirtualVoxelPatchPageIndexWithMipDataCS::FParameters>();
		Parameters->MipIt = LastMipIt;
		Parameters->PageResolution = VoxelResources.Parameters.Common.PageResolution;
		Parameters->PageCountResolution = VoxelResources.Parameters.Common.PageCountResolution;
		Parameters->DensityTexture = VoxelDensityTexture;
		Parameters->PageToPageIndexBuffer = PageToPageIndexBufferSRV;
		Parameters->OutPageIndexBuffer = PageIndexBufferUAV;
		Parameters->IndirectDispatchArgs = MipIndirectArgsBuffers[LastMipIt-1];

		TShaderMapRef<FVirtualVoxelPatchPageIndexWithMipDataCS> ComputeShader(View.ShaderMap);
		ClearUnusedGraphResources(ComputeShader, Parameters);
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("HairStrandsPatchPageIndexWithMip"),
			Parameters,
			ERDGPassFlags::Compute,
			[Parameters, ComputeShader](FRHICommandList& RHICmdList)
		{
			FComputeShaderUtils::DispatchIndirect(RHICmdList, ComputeShader, *Parameters, Parameters->IndirectDispatchArgs->GetIndirectRHICallBuffer(), 0);
		});
	}
}
/////////////////////////////////////////////////////////////////////////////////////////

void VoxelizeHairStrands(
	FRHICommandListImmediate& RHICmdList, 
	const FScene* Scene, 
	const TArray<FViewInfo>& Views,
	FHairStrandsMacroGroupViews& MacroGroupsViews)
{
	if (!IsHairStrandsVoxelizationEnable())
		return;

	FHairStrandsMacroGroupViews PrimitivesClusterViews;
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{			
		if (ViewIndex >= MacroGroupsViews.Views.Num())
			continue;

		const FViewInfo& View = Views[ViewIndex];
		FHairStrandsMacroGroupDatas& MacroGroupDatas = MacroGroupsViews.Views[ViewIndex];

		if (MacroGroupDatas.Datas.Num() == 0)
			continue;

		DECLARE_GPU_STAT(HairStrandsVoxelization);
		SCOPED_DRAW_EVENT(RHICmdList, HairStrandsVoxelization);
		SCOPED_GPU_STAT(RHICmdList, HairStrandsVoxelization);

		if (MacroGroupDatas.Datas.Num() > 0)
		{
			// Toto moves this function into the render graph. At the moment this is not possible as this functions 
			// generates internally a non-transient constant buffer which initialized VirtualVoxelResources. This 
			// needs to be rewritten/worked out.
			TRefCountPtr<FPooledRDGBuffer> PageToPageIndexBuffer;
			MacroGroupDatas.VirtualVoxelResources = AllocateVirtualVoxelResources(RHICmdList, View, MacroGroupDatas, PageToPageIndexBuffer);

			FRDGBuilder GraphBuilder(RHICmdList);
			FRDGBufferRef ClearIndArgsBuffer = IndirectVoxelPageClear(GraphBuilder, View, MacroGroupDatas.VirtualVoxelResources);

			for (FHairStrandsMacroGroupData& MacroGroup : MacroGroupDatas.Datas)
			{
				AddVirtualVoxelizationRasterPass(GraphBuilder, Scene, &View, MacroGroupDatas.VirtualVoxelResources, MacroGroup);
			}

			if (GHairVoxelInjectOpaqueDepthEnable > 0)
			{
				for (FHairStrandsMacroGroupData& MacroGroup : MacroGroupDatas.Datas)
				{
					AddVirtualVoxelInjectOpaquePass(GraphBuilder, View, MacroGroupDatas.VirtualVoxelResources, MacroGroup);
				}					
			}

			AddVirtualVoxelGenerateMipPass(GraphBuilder, View, MacroGroupDatas, ClearIndArgsBuffer, PageToPageIndexBuffer);

			GraphBuilder.Execute();
		}
	}
}
