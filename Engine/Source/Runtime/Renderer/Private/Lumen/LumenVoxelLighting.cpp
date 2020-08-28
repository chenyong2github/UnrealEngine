// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenVoxelLighting.cpp
=============================================================================*/

#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "VolumeLighting.h"
#include "LumenSceneUtils.h"
#include "LumenSceneBVH.h"
#include "DistanceFieldLightingShared.h"
#include "LumenCubeMapTree.h"

int32 GLumenSceneClipmapResolution = 64;
FAutoConsoleVariableRef CVarLumenSceneClipmapResolution(
	TEXT("r.LumenScene.ClipmapResolution"),
	GLumenSceneClipmapResolution,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

int32 GLumenSceneClipmapZResolutionDivisor = 1;
FAutoConsoleVariableRef CVarLumenSceneClipmapZResolutionDivisor(
	TEXT("r.LumenScene.ClipmapZResolutionDivisor"),
	GLumenSceneClipmapZResolutionDivisor,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

int32 GLumenSceneNumClipmapLevels = 4;
FAutoConsoleVariableRef CVarLumenSceneNumClipmapLevels(
	TEXT("r.LumenScene.NumClipmapLevels"),
	GLumenSceneNumClipmapLevels,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

float GLumenSceneFirstClipmapWorldExtent = 2500.0f;
FAutoConsoleVariableRef CVarLumenSceneClipmapWorldExtent(
	TEXT("r.LumenScene.ClipmapWorldExtent"),
	GLumenSceneFirstClipmapWorldExtent,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

int32 GLumenSceneVoxelLightingBVHCulling = 1;
FAutoConsoleVariableRef CVarLumenSceneVoxelLightingBVHCulling(
	TEXT("r.LumenScene.VoxelLightingBVHCulling"),
	GLumenSceneVoxelLightingBVHCulling,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenSceneVoxelLightingBVHCullingGridFactor = 4;
FAutoConsoleVariableRef CVarLumenSceneVoxelLightingBVHCullingGridFactor(
	TEXT("r.LumenScene.VoxelLightingBVHCullingGridFactor"),
	GLumenSceneVoxelLightingBVHCullingGridFactor,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenSceneVoxelLightingRasterizerScatter = 1;
FAutoConsoleVariableRef CVarLumenSceneVoxelLightingRasterizerScatter(
	TEXT("r.LumenScene.VoxelLightingRasterizerScatter"),
	GLumenSceneVoxelLightingRasterizerScatter,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenSceneVoxelLightingVisBuffer = 1;
FAutoConsoleVariableRef CVarLumenSceneVoxelLightingVisBuffer(
	TEXT("r.LumenScene.VoxelLightingVisBuffer"),
	GLumenSceneVoxelLightingVisBuffer,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenSceneVoxelLightingDistantScene = 1;
FAutoConsoleVariableRef CVarLumenSceneVoxelLightingDistantScene(
	TEXT("r.LumenScene.VoxelLightingDistantScene"),
	GLumenSceneVoxelLightingDistantScene,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenSceneVoxelLightingComputeScatter = 1;
FAutoConsoleVariableRef CVarLumenSceneVoxelLightingComputeScatter(
	TEXT("r.LumenScene.VoxelLightingComputeScatter"),
	GLumenSceneVoxelLightingComputeScatter,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenSceneVoxelLightingCubeMapTree = 1;
FAutoConsoleVariableRef GVarLumenSceneVoxelLightingCubeMapTree(
	TEXT("r.LumenScene.VoxelLightingCubeMapTree"),
	GLumenSceneVoxelLightingCubeMapTree,
	TEXT("Whether to use cube map trees to apply texture on mesh SDF hit points during voxelization."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenSceneVoxelLightingTraceMeshSDF = 1;
FAutoConsoleVariableRef GVarLumenSceneVoxelLightingTraceMeshSDF(
	TEXT("r.LumenScene.VoxelLightingTraceMeshSDF"),
	GLumenSceneVoxelLightingTraceMeshSDF,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenSceneVoxelLightingMeshSDFRadiusThreshold = 100;
FAutoConsoleVariableRef CVarLumenSceneVoxelLightingMeshSDFRadiusThreshold(
	TEXT("r.LumenScene.VoxelLightingMeshSDFRadiusThreshold"),
	GLumenSceneVoxelLightingMeshSDFRadiusThreshold,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenSceneVoxelLightingMeshSDFScreenSizeThreshold = .05f;
FAutoConsoleVariableRef CVarLumenSceneVoxelLightingMeshSDFScreenSizeThreshold(
	TEXT("r.LumenScene.VoxelLightingMeshSDFScreenSizeThreshold"),
	GLumenSceneVoxelLightingMeshSDFScreenSizeThreshold,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenSceneVoxelLightingMaskDownsampleShift = 2;
FAutoConsoleVariableRef CVarLumenSceneVoxelLightingMaskDownsampleShift(
	TEXT("r.LumenScene.VoxelLightingMaskDownsampleShift"),
	GLumenSceneVoxelLightingMaskDownsampleShift,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenSceneVoxelLightingForceFullUpdate = 0;
FAutoConsoleVariableRef CVarLumenSceneVoxelLightingForceFullUpdate(
	TEXT("r.LumenScene.VoxelLightingForceFullUpdate"),
	GLumenSceneVoxelLightingForceFullUpdate,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenSceneVoxelLightingForceUpdateClipmapIndex = -1;
FAutoConsoleVariableRef CVarLumenSceneVoxelLightingForceClipmapIndex(
	TEXT("r.LumenScene.VoxelLightingForceUpdateClipmapIndex"),
	GLumenSceneVoxelLightingForceUpdateClipmapIndex,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

uint32 GetClipmapResolutionXY()
{
	return FMath::Clamp<uint32>(GLumenSceneClipmapResolution, 1 << GLumenSceneVoxelLightingMaskDownsampleShift, 512);
}

int32 GetClipmapResolutionZ()
{
	return GetClipmapResolutionXY() / FMath::Clamp(GLumenSceneClipmapZResolutionDivisor, 1, 8);
}

FIntVector GetClipmapResolution()
{
	return FIntVector(GetClipmapResolutionXY(), GetClipmapResolutionXY(), GetClipmapResolutionZ());
}

int32 GetNumLumenVoxelClipmaps()
{
	int32 WantedClipmaps = GLumenSceneNumClipmapLevels;

	if (GLumenFastCameraMode != 0 && GLumenDistantScene == 0)
	{
		WantedClipmaps++;
	}

	return FMath::Clamp(WantedClipmaps, 1, MaxVoxelClipmapLevels);
}

class FVoxelLightingClipmap
{
public:
	FVector WorldMin;
	FVector WorldExtent;
	FVector VoxelSize;
	FVector ToGridScale;
	FVector ToGridBias;

	FVector4 GetVoxelSizeAndRadius()
	{
		FVector4 VoxelSizeAndRadius;
		VoxelSizeAndRadius = VoxelSize;
		VoxelSizeAndRadius.W = (0.5f * VoxelSize).Size();
		return VoxelSizeAndRadius;
	}
};

void ComputeVoxelLightingClipmap(FVoxelLightingClipmap& OutClipmap, const FVector& LumenSceneCameraOrigin, int32 ClipmapIndex, FIntVector VoxelGridResolution)
{
	const FVector FirstClipmapWorldExtent(GLumenSceneFirstClipmapWorldExtent, GLumenSceneFirstClipmapWorldExtent, GLumenSceneFirstClipmapWorldExtent / GLumenSceneClipmapZResolutionDivisor);

	const float ClipmapWorldScale = (float)(1 << ClipmapIndex);
	FVector ClipmapCenter = LumenSceneCameraOrigin;
	const FVector CellSize = (ClipmapWorldScale * FirstClipmapWorldExtent * 2) / (FVector)GetClipmapResolution();
	FIntVector GridCenter;
	GridCenter.X = FMath::FloorToInt(ClipmapCenter.X / CellSize.X);
	GridCenter.Y = FMath::FloorToInt(ClipmapCenter.Y / CellSize.Y);
	GridCenter.Z = FMath::FloorToInt(ClipmapCenter.Z / CellSize.Z);
	ClipmapCenter = FVector(GridCenter) * CellSize;

	const FVector ClipmapWorldExtent = FirstClipmapWorldExtent * ClipmapWorldScale;
	const FVector ClipmapWorldMin = ClipmapCenter - ClipmapWorldExtent;
	const FVector GridVoxelSize = 2 * ClipmapWorldExtent / FVector(VoxelGridResolution);

	OutClipmap.WorldMin = ClipmapWorldMin;
	OutClipmap.WorldExtent = ClipmapWorldExtent;
	OutClipmap.VoxelSize = GridVoxelSize;

	OutClipmap.ToGridScale = FVector(1.0f, 1.0f, 1.0f) / GridVoxelSize;
	OutClipmap.ToGridBias = -ClipmapWorldMin / GridVoxelSize + 0.5f;
}

FVector GetLumenSceneViewOrigin(const FViewInfo& View, int32 ClipmapIndex)
{
	FVector CameraOrigin = View.ViewMatrices.GetViewOrigin();

	if (View.ViewState)
	{
		FVector CameraVelocityOffset = View.ViewState->GlobalDistanceFieldCameraVelocityOffset;

		if (ClipmapIndex > 0)
		{
			const FIntVector VoxelGridResolution = GetClipmapResolution();
			FVoxelLightingClipmap Clipmap;
			ComputeVoxelLightingClipmap(Clipmap, CameraOrigin, ClipmapIndex, VoxelGridResolution);

			const FVector ClipmapExtent = Clipmap.WorldExtent;
			const float MaxCameraDriftFraction = .75f;
			CameraVelocityOffset.X = FMath::Clamp<float>(CameraVelocityOffset.X, -ClipmapExtent.X * MaxCameraDriftFraction, ClipmapExtent.X * MaxCameraDriftFraction);
			CameraVelocityOffset.Y = FMath::Clamp<float>(CameraVelocityOffset.Y, -ClipmapExtent.Y * MaxCameraDriftFraction, ClipmapExtent.Y * MaxCameraDriftFraction);
			CameraVelocityOffset.Z = FMath::Clamp<float>(CameraVelocityOffset.Z, -ClipmapExtent.Z * MaxCameraDriftFraction, ClipmapExtent.Z * MaxCameraDriftFraction);
		}

		CameraOrigin += CameraVelocityOffset;
	}

	return CameraOrigin;
}

class FVoxelLightingBVHCullingCS : public FBVHCullingBaseCS
{
	DECLARE_GLOBAL_SHADER(FVoxelLightingBVHCullingCS)
	SHADER_USE_PARAMETER_STRUCT(FVoxelLightingBVHCullingCS, FBVHCullingBaseCS)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FBVHCullingParameters, BVHCullingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
		SHADER_PARAMETER(FVector, GridMin)
		SHADER_PARAMETER(FVector, GridVoxelSize)
		SHADER_PARAMETER(float, GridConeRadiusSq)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FVoxelLightingBVHCullingCS, "/Engine/Private/Lumen/LumenVoxelLighting.usf", "BVHCullingCS", SF_Compute);

FIntVector ComputeVoxelLightingGroupSize(8, 8, 1);

class FComputeVoxelLightingGatherCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FComputeVoxelLightingGatherCS)
	SHADER_USE_PARAMETER_STRUCT(FComputeVoxelLightingGatherCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, RWVoxelLighting)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CulledCardGridHeader)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CulledCardGridData)
		SHADER_PARAMETER(FIntVector, GridResolution)
		SHADER_PARAMETER(uint32, TargetClipmapIndex)
		SHADER_PARAMETER(FVector, GridMin)
		SHADER_PARAMETER(FVector, GridVoxelSize)
		SHADER_PARAMETER(FIntVector, CullGridSize)
		SHADER_PARAMETER(uint32, CullGridFactor)
		SHADER_PARAMETER(uint32, VoxelRayTracing)
	END_SHADER_PARAMETER_STRUCT()

	class FCulledCardsGrid : SHADER_PERMUTATION_BOOL("CULLED_CARDS_GRID");
	using FPermutationDomain = TShaderPermutationDomain<FCulledCardsGrid>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), ComputeVoxelLightingGroupSize.X);
	}
};

IMPLEMENT_GLOBAL_SHADER(FComputeVoxelLightingGatherCS, "/Engine/Private/Lumen/LumenVoxelLighting.usf", "ComputeVoxelLightingGatherCS", SF_Compute);

class FMergeVoxelLightingCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMergeVoxelLightingCS)
	SHADER_USE_PARAMETER_STRUCT(FMergeVoxelLightingCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, RWMergedVoxelLighting)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, FaceVoxelLighting)
		SHADER_PARAMETER(uint32, TargetClipmapIndex)
		SHADER_PARAMETER(FIntVector, GridResolution)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), ComputeVoxelLightingGroupSize.X);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMergeVoxelLightingCS, "/Engine/Private/Lumen/LumenVoxelLighting.usf", "MergeVoxelLightingCS", SF_Compute);

void InjectCardsWithComputeGather(
	const FViewInfo& View,
	const FLumenCardTracingInputs& TracingInputs,
	FRDGTextureRef VoxelLighting,
	const TArray<int32, SceneRenderingAllocator>& ClipmapsToUpdate,
	FRDGBuilder& GraphBuilder)
{
	LLM_SCOPE(ELLMTag::Lumen);

	const FIntVector VoxelGridResolution = GetClipmapResolution();

	const int32 CullGridFactor = FMath::Clamp(GLumenSceneVoxelLightingBVHCullingGridFactor, 1, GLumenSceneClipmapResolution);
	const int32 CullGridRes = GetClipmapResolutionXY() / CullGridFactor;
	const FIntVector CullGridSize = FIntVector(CullGridRes, CullGridRes, GetClipmapResolutionZ() / CullGridFactor);
	
	FBVHCulling BVHCulling[MaxVoxelClipmapLevels];
	if (GLumenSceneVoxelLightingBVHCulling)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "VoxelLightingBVHCulling");

		for (int32 ClipmapIndex : ClipmapsToUpdate)
		{
			BVHCulling[ClipmapIndex].Init(GraphBuilder, View.ShaderMap, CullGridSize);
		}

		for (int32 BVHLevel = 0; BVHLevel < FMath::Max(1, TracingInputs.BVHDepth); ++BVHLevel)
		{
			for (int32 ClipmapIndex : ClipmapsToUpdate)
			{
				BVHCulling[ClipmapIndex].InitNextPass(GraphBuilder, View.ShaderMap, BVHLevel);
			}

			// Run pass for the current BVH level.
			for (int32 ClipmapIndex : ClipmapsToUpdate)
			{
				FVoxelLightingBVHCullingCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVoxelLightingBVHCullingCS::FParameters>();
				PassParameters->BVHCullingParameters = BVHCulling[ClipmapIndex].BVHCullingParameters;

				GetLumenCardTracingParameters(View, TracingInputs, PassParameters->TracingParameters, true);

				FVoxelLightingClipmap Clipmap;
				const FVector LumenSceneCameraOrigin = GetLumenSceneViewOrigin(View, ClipmapIndex);
				ComputeVoxelLightingClipmap(Clipmap, LumenSceneCameraOrigin, ClipmapIndex, VoxelGridResolution);
				const float GridConeRadius = (Clipmap.VoxelSize * 0.5f).GetAbsMax();
				PassParameters->GridMin = Clipmap.WorldMin;
				PassParameters->GridVoxelSize = Clipmap.VoxelSize * CullGridFactor;
				PassParameters->GridConeRadiusSq = GridConeRadius * GridConeRadius;

				BVHCulling[ClipmapIndex].NextPass<FVoxelLightingBVHCullingCS>(GraphBuilder, View.ShaderMap, BVHLevel, PassParameters);
			}
		}

		for (int32 ClipmapIndex : ClipmapsToUpdate)
		{
			BVHCulling[ClipmapIndex].CompactListIntoGrid(GraphBuilder, View.ShaderMap);
		}
	}

	FRDGTextureUAVRef VoxelLightingUAV = GraphBuilder.CreateUAV(VoxelLighting);

	for (int32 ClipmapIndex : ClipmapsToUpdate)
	{
		FComputeVoxelLightingGatherCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FComputeVoxelLightingGatherCS::FParameters>();
		PassParameters->RWVoxelLighting = VoxelLightingUAV;

		GetLumenCardTracingParameters(View, TracingInputs, PassParameters->TracingParameters, true);
		PassParameters->TargetClipmapIndex = ClipmapIndex;
		PassParameters->GridResolution = VoxelGridResolution;

		FVoxelLightingClipmap Clipmap;
		const FVector LumenSceneCameraOrigin = GetLumenSceneViewOrigin(View, ClipmapIndex);
		ComputeVoxelLightingClipmap(Clipmap, LumenSceneCameraOrigin, ClipmapIndex, VoxelGridResolution);
		PassParameters->GridMin = Clipmap.WorldMin;
		PassParameters->GridVoxelSize = Clipmap.VoxelSize;

		PassParameters->CulledCardGridHeader = BVHCulling[ClipmapIndex].CulledCardGridHeaderSRV;
		PassParameters->CulledCardGridData = BVHCulling[ClipmapIndex].CulledCardGridDataSRV;
		PassParameters->CullGridSize = CullGridSize;
		PassParameters->CullGridFactor = CullGridFactor;

		PassParameters->VoxelRayTracing = Lumen::UseVoxelRayTracing();

		FComputeVoxelLightingGatherCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FComputeVoxelLightingGatherCS::FCulledCardsGrid>(GLumenSceneVoxelLightingBVHCulling != 0);
		auto ComputeShader = View.ShaderMap->GetShader<FComputeVoxelLightingGatherCS>(PermutationVector);

		const FIntVector GroupSize = FComputeShaderUtils::GetGroupCount(PassParameters->GridResolution, ComputeVoxelLightingGroupSize);
	
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("ComputeVoxelLighting %ux%ux%u", GetClipmapResolutionXY(), GetClipmapResolutionXY(), GetClipmapResolutionZ()),
			ComputeShader,
			PassParameters,
			GroupSize);
	}
}

uint32 SetupCardScatterInstancesGroupSize = 64;

class FSetupCardScatterInstancesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSetupCardScatterInstancesCS);
	SHADER_USE_PARAMETER_STRUCT(FSetupCardScatterInstancesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWQuadAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWQuadData)
		SHADER_PARAMETER_STRUCT_REF(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER(uint32, NumClipmaps)
		SHADER_PARAMETER_ARRAY(FVector4, ClipmapWorldCenter, [MaxVoxelClipmapLevels])
		SHADER_PARAMETER_ARRAY(FVector4, ClipmapWorldExtent, [MaxVoxelClipmapLevels])
		SHADER_PARAMETER_ARRAY(FVector4, ClipmapWorldMin, [MaxVoxelClipmapLevels])
		SHADER_PARAMETER_ARRAY(FVector4, ClipmapWorldSize, [MaxVoxelClipmapLevels])
		SHADER_PARAMETER_ARRAY(FVector4, ClipmapVoxelSizeAndRadius, [MaxVoxelClipmapLevels])
		SHADER_PARAMETER(FIntVector, GridResolution)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), SetupCardScatterInstancesGroupSize);
	}
};

IMPLEMENT_GLOBAL_SHADER(FSetupCardScatterInstancesCS, "/Engine/Private/Lumen/LumenVoxelLighting.usf", "SetupCardScatterInstancesCS", SF_Compute);

uint32 SetupMeshSDFScatterInstancesGroupSize = 64;

class FSetupMeshSDFScatterInstancesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSetupMeshSDFScatterInstancesCS);
	SHADER_USE_PARAMETER_STRUCT(FSetupMeshSDFScatterInstancesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWQuadAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWQuadData)
		SHADER_PARAMETER_STRUCT_REF(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(uint32, NumClipmaps)
		SHADER_PARAMETER(uint32, OutermostClipmapIndex)
		SHADER_PARAMETER_ARRAY(FVector4, ClipmapWorldMin, [MaxVoxelClipmapLevels])
		SHADER_PARAMETER_ARRAY(FVector4, ClipmapWorldSize, [MaxVoxelClipmapLevels])
		SHADER_PARAMETER_ARRAY(FVector4, ClipmapWorldCenter, [MaxVoxelClipmapLevels])
		SHADER_PARAMETER_ARRAY(FVector4, ClipmapWorldExtent, [MaxVoxelClipmapLevels])
		SHADER_PARAMETER_ARRAY(FVector4, ClipmapVoxelSizeAndRadius, [MaxVoxelClipmapLevels])
		SHADER_PARAMETER_ARRAY(FVector, ClipmapToGridScale, [MaxVoxelClipmapLevels])
		SHADER_PARAMETER_ARRAY(FVector, ClipmapToGridBias, [MaxVoxelClipmapLevels])
		SHADER_PARAMETER(FIntVector, GridResolution)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, SceneObjectBounds)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, SceneObjectData)
		SHADER_PARAMETER(uint32, NumSceneObjects)
		SHADER_PARAMETER(float, MeshSDFRadiusThreshold)
		SHADER_PARAMETER(float, MeshSDFScreenSizeThreshold)
	END_SHADER_PARAMETER_STRUCT()

	class FComputeScatter : SHADER_PERMUTATION_BOOL("COMPUTE_SCATTER");
	class FSingleClipmapToUpdate : SHADER_PERMUTATION_BOOL("SINGLE_CLIPMAP_TO_UPDATE");
	using FPermutationDomain = TShaderPermutationDomain<FComputeScatter, FSingleClipmapToUpdate>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), SetupMeshSDFScatterInstancesGroupSize);
	}
};

IMPLEMENT_GLOBAL_SHADER(FSetupMeshSDFScatterInstancesCS, "/Engine/Private/Lumen/LumenVoxelLighting.usf", "SetupMeshSDFScatterInstancesCS", SF_Compute);

class FClearVoxelMaskCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClearVoxelMaskCS)
	SHADER_USE_PARAMETER_STRUCT(FClearVoxelMaskCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, RWVoxelMask)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), ComputeVoxelLightingGroupSize.X);
	}
};

IMPLEMENT_GLOBAL_SHADER(FClearVoxelMaskCS, "/Engine/Private/Lumen/LumenVoxelLighting.usf", "ClearVoxelMaskCS", SF_Compute);

class FCardVoxelizeVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCardVoxelizeVS);
	SHADER_USE_PARAMETER_STRUCT(FCardVoxelizeVS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint2>, QuadData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, QuadAllocator)
		SHADER_PARAMETER_STRUCT_REF(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER(uint32, NumClipmaps)
		SHADER_PARAMETER_ARRAY(FVector4, ClipmapWorldToUVScale, [MaxVoxelClipmapLevels])
		SHADER_PARAMETER_ARRAY(FVector4, ClipmapWorldToUVBias, [MaxVoxelClipmapLevels])
		SHADER_PARAMETER_ARRAY(FVector4, ClipmapWorldMin, [MaxVoxelClipmapLevels])
		SHADER_PARAMETER_ARRAY(FVector4, ClipmapWorldSize, [MaxVoxelClipmapLevels])
		SHADER_PARAMETER_ARRAY(FVector4, ClipmapVoxelSizeAndRadius, [MaxVoxelClipmapLevels])
		SHADER_PARAMETER(FIntVector, GridResolution)
		SHADER_PARAMETER(uint32, TilesPerInstance)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, SceneObjectBounds)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, SceneObjectData)
	END_SHADER_PARAMETER_STRUCT()

	class FTraceMeshSDF : SHADER_PERMUTATION_BOOL("CARD_TRACE_MESH_SDF");
	using FPermutationDomain = TShaderPermutationDomain<FTraceMeshSDF>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FCardVoxelizeVS, "/Engine/Private/Lumen/LumenVoxelLighting.usf", "CardVoxelizeVS", SF_Vertex);


class FCardVoxelizeMaskSetupPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCardVoxelizeMaskSetupPS);
	SHADER_USE_PARAMETER_STRUCT(FCardVoxelizeMaskSetupPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, RWVoxelMask)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenMeshSDFTracingParameters, MeshSDFTracingParameters)
		SHADER_PARAMETER(uint32, NumClipmaps)
		SHADER_PARAMETER_ARRAY(FVector4, ClipmapWorldMin, [MaxVoxelClipmapLevels])
		SHADER_PARAMETER_ARRAY(FVector4, ClipmapWorldSize, [MaxVoxelClipmapLevels])
		SHADER_PARAMETER(FIntVector, GridResolution)
	END_SHADER_PARAMETER_STRUCT()

	class FTraceMeshSDF : SHADER_PERMUTATION_BOOL("CARD_TRACE_MESH_SDF");
	using FPermutationDomain = TShaderPermutationDomain<FTraceMeshSDF>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FCardVoxelizeMaskSetupPS, "/Engine/Private/Lumen/LumenVoxelLighting.usf", "CardVoxelizeMaskSetupPS", SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FCardVoxelizeMask, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FCardVoxelizeVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FCardVoxelizeMaskSetupPS::FParameters, PS)
	SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, CardIndirectArgs)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()


class FClearVoxelLightingCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClearVoxelLightingCS)
	SHADER_USE_PARAMETER_STRUCT(FClearVoxelLightingCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, RWVoxelOITLighting)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, RWVoxelOITTransparency)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<uint>, VoxelMask)
		SHADER_PARAMETER(uint32, VoxelMaskResolutionShift)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), ComputeVoxelLightingGroupSize.X);
	}
};

IMPLEMENT_GLOBAL_SHADER(FClearVoxelLightingCS, "/Engine/Private/Lumen/LumenVoxelLighting.usf", "ClearVoxelLightingCS", SF_Compute);


class FCardVoxelizePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCardVoxelizePS);
	SHADER_USE_PARAMETER_STRUCT(FCardVoxelizePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, RWVoxelOITLighting)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, RWVoxelOITTransparency)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, RWVoxelVisBuffer)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenMeshSDFTracingParameters, MeshSDFTracingParameters)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<uint>, VoxelMask)
		SHADER_PARAMETER(uint32, VoxelMaskResolutionShift)
		SHADER_PARAMETER(uint32, NumClipmaps)
		SHADER_PARAMETER_ARRAY(FVector4, ClipmapWorldMin, [MaxVoxelClipmapLevels])
		SHADER_PARAMETER_ARRAY(FVector4, ClipmapWorldSize, [MaxVoxelClipmapLevels])
		SHADER_PARAMETER(FIntVector, GridResolution)
		SHADER_PARAMETER(uint32, VoxelRayTracing)
	END_SHADER_PARAMETER_STRUCT()

	class FTraceMeshSDF : SHADER_PERMUTATION_BOOL("CARD_TRACE_MESH_SDF");
	class FCubeMapTree : SHADER_PERMUTATION_BOOL("CUBE_MAP_TREE");
	class FVoxelVisBuffer : SHADER_PERMUTATION_BOOL("VOXEL_VIS_BUFFER");
	using FPermutationDomain = TShaderPermutationDomain<FTraceMeshSDF, FCubeMapTree, FVoxelVisBuffer>;

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		if (!PermutationVector.Get<FTraceMeshSDF>())
		{
			PermutationVector.Set<FCubeMapTree>(false);
			PermutationVector.Set<FVoxelVisBuffer>(false);
		}

		if (PermutationVector.Get<FVoxelVisBuffer>())
		{
			PermutationVector.Set<FCubeMapTree>(true);
		}

		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if (RemapPermutation(PermutationVector) != PermutationVector)
		{
			return false;
		}

		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FCardVoxelizePS, "/Engine/Private/Lumen/LumenVoxelLighting.usf", "CardVoxelizePS", SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FCardVoxelize, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FCardVoxelizeVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FCardVoxelizePS::FParameters, PS)
	SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, CardIndirectArgs)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()


class FCompactVoxelLightingCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCompactVoxelLightingCS)
	SHADER_USE_PARAMETER_STRUCT(FCompactVoxelLightingCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, RWVoxelLighting)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, VoxelOITLighting)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, VoxelOITTransparency)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<uint>, VoxelMask)
		SHADER_PARAMETER(FIntVector, GridResolution)
		SHADER_PARAMETER(FIntVector, ClipmapTextureResolution)
		SHADER_PARAMETER(uint32, VoxelMaskResolutionShift)
		SHADER_PARAMETER(uint32, SourceClipmapIndex)
		SHADER_PARAMETER(uint32, DestClipmapIndex)
		SHADER_PARAMETER_ARRAY(FVector4, ClipmapTextureYOffset, [MaxVoxelClipmapLevels])
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), ComputeVoxelLightingGroupSize.X);
	}
};

IMPLEMENT_GLOBAL_SHADER(FCompactVoxelLightingCS, "/Engine/Private/Lumen/LumenVoxelLighting.usf", "CompactVoxelLightingCS", SF_Compute);

class FSetupComputeScaterIndirectArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSetupComputeScaterIndirectArgsCS)
	SHADER_USE_PARAMETER_STRUCT(FSetupComputeScaterIndirectArgsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWObjectIndirectArguments)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, QuadAllocator)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FSetupComputeScaterIndirectArgsCS, "/Engine/Private/Lumen/LumenVoxelLighting.usf", "SetupComputeScaterIndirectArgsCS", SF_Compute);

class FComputeScatterCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FComputeScatterCS)
	SHADER_USE_PARAMETER_STRUCT(FComputeScatterCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, RWVoxelVisBuffer)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenMeshSDFTracingParameters, MeshSDFTracingParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, QuadAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, QuadData)
		SHADER_PARAMETER_ARRAY(FVector4, ClipmapWorldMin, [MaxVoxelClipmapLevels])
		SHADER_PARAMETER_ARRAY(FVector4, ClipmapWorldSize, [MaxVoxelClipmapLevels])
		SHADER_PARAMETER_ARRAY(FVector, ClipmapToGridScale, [MaxVoxelClipmapLevels])
		SHADER_PARAMETER_ARRAY(FVector, ClipmapToGridBias, [MaxVoxelClipmapLevels])
		SHADER_PARAMETER(FIntVector, GridResolution)
		SHADER_PARAMETER(uint32, VoxelRayTracing)
		SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, ComputeScatterIndirectArgsBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	class FSingleClipmapToUpdate : SHADER_PERMUTATION_BOOL("SINGLE_CLIPMAP_TO_UPDATE");
	using FPermutationDomain = TShaderPermutationDomain<FSingleClipmapToUpdate>;

	static int32 GetGroupSize()
	{
		return 64;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
		OutEnvironment.SetDefine(TEXT("CARD_TRACE_MESH_SDF"), 1);
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
	}
};

IMPLEMENT_GLOBAL_SHADER(FComputeScatterCS, "/Engine/Private/Lumen/LumenVoxelLighting.usf", "ComputeScatterCS", SF_Compute);

class FVoxelVisBufferShadingCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVoxelVisBufferShadingCS)
	SHADER_USE_PARAMETER_STRUCT(FVoxelVisBufferShadingCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, RWVoxelLighting)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenMeshSDFTracingParameters, MeshSDFTracingParameters)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, VoxelVisBuffer)
		SHADER_PARAMETER(uint32, SourceClipmapIndex)
		SHADER_PARAMETER(uint32, TargetClipmapIndex)
		SHADER_PARAMETER(FVector, GridMin)
		SHADER_PARAMETER(FVector, GridVoxelSize)
		SHADER_PARAMETER(FIntVector, ClipmapGridResolution)
		SHADER_PARAMETER(FIntVector, OutputGridResolution)
		SHADER_PARAMETER(uint32, VoxelRayTracing)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	class FDistantScene : SHADER_PERMUTATION_BOOL("DISTANT_SCENE");
	using FPermutationDomain = TShaderPermutationDomain<FDistantScene>;

	static FIntVector GetGroupSize()
	{
		return FIntVector(4, 4, 4);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize().X);
		OutEnvironment.SetDefine(TEXT("CARD_TRACE_MESH_SDF"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FVoxelVisBufferShadingCS, "/Engine/Private/Lumen/LumenVoxelLighting.usf", "VoxelVisBufferShadingCS", SF_Compute);

void InjectCardsWithRasterizerScatter(
	const FViewInfo& View,
	FScene* Scene,
	const FLumenCardTracingInputs& TracingInputs,
	FRDGTextureRef VoxelLighting,
	const TArray<int32, SceneRenderingAllocator>& ClipmapsToUpdate,
	FRDGBuilder& GraphBuilder)
{
	LLM_SCOPE(ELLMTag::Lumen);

	const FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;
	const FDistanceFieldSceneData& DistanceFieldSceneData = Scene->DistanceFieldSceneData;
	const FIntVector VoxelGridResolution = GetClipmapResolution();
	const bool bUseVoxelVisBuffer = GLumenSceneVoxelLightingTraceMeshSDF != 0 && GLumenSceneVoxelLightingVisBuffer != 0;
	const bool bUseComputeScatter = bUseVoxelVisBuffer && GLumenSceneVoxelLightingComputeScatter != 0;

	int32 MaxObjects = GLumenSceneVoxelLightingTraceMeshSDF ? DistanceFieldSceneData.NumObjectsInBuffer : LumenSceneData.Cards.Num();
	if (MaxObjects == 0)
	{
		// Nothing to voxelize. Just clear voxel lighting and return.
		const FLinearColor VoxelLightingClearValue(0.0f, 0.0f, 0.0f, 1.0f);
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(VoxelLighting), VoxelLightingClearValue);
		return;
	}

	ensureMsgf(MaxObjects < (1 << 24), TEXT("Object index won't fit into 24 bits, fix SetupCardScatterInstancesCS packing"));

	FRDGBufferRef QuadAllocatorBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("QuadAllocatorBuffer"));
	FRDGBufferUAVRef QuadAllocatorUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(QuadAllocatorBuffer, PF_R32_UINT));
	FRDGBufferSRVRef QuadAllocatorSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(QuadAllocatorBuffer, PF_R32_UINT));

	FComputeShaderUtils::ClearUAV(GraphBuilder, View.ShaderMap, QuadAllocatorUAV, 0);

	int32 MaxCubeMapTrees = FMath::RoundUpToPowerOfTwo(LumenSceneData.CubeMapTrees.Num());

	int32 MaxQuads = MaxObjects * 6 * ClipmapsToUpdate.Num();
	if (bUseComputeScatter)
	{
		const int32 AverageQuadsPerObject = 32;
		MaxQuads = 2 * FMath::Max(MaxObjects, 1024) * ClipmapsToUpdate.Num() * AverageQuadsPerObject;
	}
	MaxQuads = FMath::RoundUpToPowerOfTwo(FMath::Max(MaxQuads, 1));

	FRDGBufferRef QuadDataBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), MaxQuads), TEXT("QuadDataBuffer"));
	FRDGBufferUAVRef QuadDataUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(QuadDataBuffer, PF_R32_UINT));
	FRDGBufferSRVRef QuadDataSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(QuadDataBuffer, PF_R32_UINT));

	FLumenMeshSDFTracingParameters MeshSDFTracingParameters;
	FMemory::Memzero(MeshSDFTracingParameters);

	if (GLumenSceneVoxelLightingTraceMeshSDF)
	{
		{
			FSetupMeshSDFScatterInstancesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSetupMeshSDFScatterInstancesCS::FParameters>();
			PassParameters->RWQuadAllocator = QuadAllocatorUAV;
			PassParameters->RWQuadData = QuadDataUAV;

			PassParameters->LumenCardScene = LumenSceneData.UniformBuffer;
			PassParameters->View = View.ViewUniformBuffer;
			PassParameters->OutermostClipmapIndex = ClipmapsToUpdate.Num() - 1;
			PassParameters->NumClipmaps = ClipmapsToUpdate.Num();
			PassParameters->GridResolution = VoxelGridResolution;

			int32 CompactedClipmapIndex = 0;

			for (int32 ClipmapIndex : ClipmapsToUpdate)
			{
				FVoxelLightingClipmap Clipmap;
				const FVector LumenSceneCameraOrigin = GetLumenSceneViewOrigin(View, ClipmapIndex);
				ComputeVoxelLightingClipmap(Clipmap, LumenSceneCameraOrigin, ClipmapIndex, VoxelGridResolution);

				PassParameters->ClipmapWorldMin[CompactedClipmapIndex] = Clipmap.WorldMin;
				PassParameters->ClipmapWorldSize[CompactedClipmapIndex] = Clipmap.WorldExtent * 2;
				PassParameters->ClipmapWorldCenter[CompactedClipmapIndex] = Clipmap.WorldMin + Clipmap.WorldExtent;
				PassParameters->ClipmapWorldExtent[CompactedClipmapIndex] = Clipmap.WorldExtent;
				PassParameters->ClipmapVoxelSizeAndRadius[CompactedClipmapIndex] = Clipmap.GetVoxelSizeAndRadius();
				PassParameters->ClipmapToGridScale[CompactedClipmapIndex] = Clipmap.ToGridScale;
				PassParameters->ClipmapToGridBias[CompactedClipmapIndex] = Clipmap.ToGridBias;
				CompactedClipmapIndex++;
			}

			PassParameters->SceneObjectBounds = DistanceFieldSceneData.GetCurrentObjectBuffers()->Bounds.SRV;
			PassParameters->SceneObjectData = DistanceFieldSceneData.GetCurrentObjectBuffers()->Data.SRV;
			PassParameters->NumSceneObjects = DistanceFieldSceneData.NumObjectsInBuffer;
			PassParameters->MeshSDFRadiusThreshold = GLumenSceneVoxelLightingMeshSDFRadiusThreshold;
			PassParameters->MeshSDFScreenSizeThreshold = GLumenSceneVoxelLightingMeshSDFScreenSizeThreshold;

			FSetupMeshSDFScatterInstancesCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FSetupMeshSDFScatterInstancesCS::FComputeScatter>(bUseComputeScatter);
			PermutationVector.Set<FSetupMeshSDFScatterInstancesCS::FSingleClipmapToUpdate>(ClipmapsToUpdate.Num() == 1);
			auto ComputeShader = View.ShaderMap->GetShader<FSetupMeshSDFScatterInstancesCS>(PermutationVector);
			const FIntVector GroupSize(FMath::DivideAndRoundUp<int32>(DistanceFieldSceneData.NumObjectsInBuffer, SetupMeshSDFScatterInstancesGroupSize), 1, 1);
			FScene* LocalScene = Scene;

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("SetupMeshSDFScatterInstances"),
				PassParameters,
				ERDGPassFlags::Compute,
				[LocalScene, PassParameters, ComputeShader, GroupSize](FRHICommandList& RHICmdList)
			{
				FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *PassParameters, GroupSize);
			});
		}

		MeshSDFTracingParameters.MeshSDFObjectOverlappingCardHeader = LumenSceneData.MeshSDFOverlappingCardHeader.SRV;
		MeshSDFTracingParameters.MeshSDFObjectOverlappingCardData = LumenSceneData.MeshSDFOverlappingCardData.SRV;

		MeshSDFTracingParameters.SceneObjectBounds = DistanceFieldSceneData.GetCurrentObjectBuffers()->Bounds.SRV;
		MeshSDFTracingParameters.SceneObjectData = DistanceFieldSceneData.GetCurrentObjectBuffers()->Data.SRV;
		MeshSDFTracingParameters.NumSceneObjects = DistanceFieldSceneData.NumObjectsInBuffer;

		MeshSDFTracingParameters.DistanceFieldTexture = GDistanceFieldVolumeTextureAtlas.VolumeTextureRHI;
		MeshSDFTracingParameters.DistanceFieldSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

		const int32 NumTexelsOneDimX = GDistanceFieldVolumeTextureAtlas.GetSizeX();
		const int32 NumTexelsOneDimY = GDistanceFieldVolumeTextureAtlas.GetSizeY();
		const int32 NumTexelsOneDimZ = GDistanceFieldVolumeTextureAtlas.GetSizeZ();
		const FVector DistanceFieldAtlasTexelSize(1.0f / NumTexelsOneDimX, 1.0f / NumTexelsOneDimY, 1.0f / NumTexelsOneDimZ);
		MeshSDFTracingParameters.DistanceFieldAtlasTexelSize = DistanceFieldAtlasTexelSize;
	}
	else
	{
		FSetupCardScatterInstancesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSetupCardScatterInstancesCS::FParameters>();
		PassParameters->RWQuadAllocator = QuadAllocatorUAV;
		PassParameters->RWQuadData = QuadDataUAV;

		PassParameters->LumenCardScene = LumenSceneData.UniformBuffer;
		PassParameters->NumClipmaps = ClipmapsToUpdate.Num();
		PassParameters->GridResolution = VoxelGridResolution;

		int32 CompactedClipmapIndex = 0;

		for (int32 ClipmapIndex : ClipmapsToUpdate)
		{
			FVoxelLightingClipmap Clipmap;
			const FVector LumenSceneCameraOrigin = GetLumenSceneViewOrigin(View, ClipmapIndex);
			ComputeVoxelLightingClipmap(Clipmap, LumenSceneCameraOrigin, ClipmapIndex, VoxelGridResolution);

			PassParameters->ClipmapWorldMin[CompactedClipmapIndex] = Clipmap.WorldMin;
			PassParameters->ClipmapWorldSize[CompactedClipmapIndex] = Clipmap.WorldExtent * 2;
			PassParameters->ClipmapWorldCenter[CompactedClipmapIndex] = Clipmap.WorldMin + Clipmap.WorldExtent;
			PassParameters->ClipmapWorldExtent[CompactedClipmapIndex] = Clipmap.WorldExtent;
			PassParameters->ClipmapVoxelSizeAndRadius[CompactedClipmapIndex] = Clipmap.GetVoxelSizeAndRadius();
			CompactedClipmapIndex++;
		}

		auto ComputeShader = View.ShaderMap->GetShader<FSetupCardScatterInstancesCS>();
		const FIntVector GroupSize(FMath::DivideAndRoundUp<int32>(LumenSceneData.Cards.Num(), SetupCardScatterInstancesGroupSize), 1, 1);
		FScene* LocalScene = Scene;

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("SetupCardScatterInstances"),
			PassParameters,
			ERDGPassFlags::Compute,
			[LocalScene, PassParameters, ComputeShader, GroupSize](FRHICommandList& RHICmdList)
		{
			FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *PassParameters, GroupSize);
		});
	}

	FIntVector ClipmapGridResolution = GetClipmapResolution();
	FIntVector VolumeTextureResolution(GetClipmapResolutionXY(), GetClipmapResolutionXY() * ClipmapsToUpdate.Num(), GetClipmapResolutionZ() * 6);

	FRDGTextureRef VoxelVisBuffer = nullptr;
	FRDGTextureUAVRef VoxelVisBufferUAV = nullptr;

	if (bUseVoxelVisBuffer)
	{
		FPooledRenderTargetDesc VoxelVisBuferDesc(FPooledRenderTargetDesc::CreateVolumeDesc(
			VolumeTextureResolution.X,
			VolumeTextureResolution.Y,
			VolumeTextureResolution.Z,
			PF_R32_UINT,
			FClearValueBinding::Transparent,
			TexCreate_None,
			TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV | TexCreate_3DTiling,
			false));
		VoxelVisBuffer = GraphBuilder.CreateTexture(VoxelVisBuferDesc, TEXT("VoxelVisBuffer"));
		VoxelVisBufferUAV = GraphBuilder.CreateUAV(VoxelVisBuffer);

		uint32 VisBufferClearValue[4] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF };
		AddClearUAVPass(GraphBuilder, VoxelVisBufferUAV, VisBufferClearValue);
	}

	FRDGBufferRef CardIndirectArgsBuffer = nullptr;
	FRDGBufferUAVRef CardIndirectArgsBufferUAV = nullptr;
	FRDGBufferRef ComputeScatterIndirectArgsBuffer = nullptr;

	if (bUseComputeScatter)
	{
		ComputeScatterIndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("ComputeScatterArgsBuffer"));

		FSetupComputeScaterIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSetupComputeScaterIndirectArgsCS::FParameters>();
		PassParameters->RWObjectIndirectArguments = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(ComputeScatterIndirectArgsBuffer));;
		PassParameters->QuadAllocator = QuadAllocatorSRV;

		auto ComputeShader = View.ShaderMap->GetShader<FSetupComputeScaterIndirectArgsCS>();

		const FIntVector GroupSize(1, 1, 1);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("SetupComputeScaterIndirectArgsCS"),
			ComputeShader,
			PassParameters,
			GroupSize);
	}
	else
	{
		CardIndirectArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDrawIndexedIndirectParameters>(1), TEXT("CardIndirectArgsBuffer"));
		CardIndirectArgsBufferUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(CardIndirectArgsBuffer));

		FInitializeCardScatterIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FInitializeCardScatterIndirectArgsCS::FParameters>();
		PassParameters->RWCardIndirectArgs = CardIndirectArgsBufferUAV;
		PassParameters->QuadAllocator = QuadAllocatorSRV;
		PassParameters->MaxScatterInstanceCount = 1;
		PassParameters->TilesPerInstance = NumLumenQuadsInBuffer;

		FInitializeCardScatterIndirectArgsCS::FPermutationDomain PermutationVector;
		PermutationVector.Set< FInitializeCardScatterIndirectArgsCS::FRectList >(UseRectTopologyForLumen());
		auto ComputeShader = View.ShaderMap->GetShader< FInitializeCardScatterIndirectArgsCS >(PermutationVector);

		const FIntVector GroupSize(1, 1, 1);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("InitializeCardScatterIndirectArgsCS"),
			ComputeShader,
			PassParameters,
			GroupSize);
	}

	FRDGTextureRef VoxelMask = nullptr;
	FRDGTextureRef VoxelOITLighting = nullptr;
	FRDGTextureRef VoxelOITTransparency = nullptr;
	FRDGTextureUAVRef VoxelOITLightingUAV = nullptr;
	FRDGTextureUAVRef VoxelOITTransparencyUAV = nullptr;

	if (!bUseVoxelVisBuffer)
	{
		FIntVector VoxelMaskTextureResolution(VolumeTextureResolution.X >> GLumenSceneVoxelLightingMaskDownsampleShift, VolumeTextureResolution.Y >> GLumenSceneVoxelLightingMaskDownsampleShift, VolumeTextureResolution.Z >> GLumenSceneVoxelLightingMaskDownsampleShift);
		FPooledRenderTargetDesc MaskDesc(FPooledRenderTargetDesc::CreateVolumeDesc(VoxelMaskTextureResolution.X, VoxelMaskTextureResolution.Y, VoxelMaskTextureResolution.Z, PF_R16_UINT, FClearValueBinding::Transparent, TexCreate_None, TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV, false));
		VoxelMask = GraphBuilder.CreateTexture(MaskDesc, TEXT("VoxelMask"));
		FRDGTextureUAVRef VoxelMaskUAV = GraphBuilder.CreateUAV(VoxelMask);

		{
			FClearVoxelMaskCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FClearVoxelMaskCS::FParameters>();
			PassParameters->RWVoxelMask = VoxelMaskUAV;

			auto ComputeShader = View.ShaderMap->GetShader<FClearVoxelMaskCS>();
			const FIntVector GroupSize = FComputeShaderUtils::GetGroupCount(VoxelMaskTextureResolution, ComputeVoxelLightingGroupSize);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("ClearVoxelMask"),
				ComputeShader,
				PassParameters,
				GroupSize);
		}

		{
			FIntVector VoxelMaskGridResolution = FIntVector(VoxelGridResolution.X >> GLumenSceneVoxelLightingMaskDownsampleShift, VoxelGridResolution.Y >> GLumenSceneVoxelLightingMaskDownsampleShift, VoxelGridResolution.Z >> GLumenSceneVoxelLightingMaskDownsampleShift);
			FCardVoxelizeMask* PassParameters = GraphBuilder.AllocParameters<FCardVoxelizeMask>();

			PassParameters->VS.QuadData = QuadDataSRV;
			PassParameters->VS.QuadAllocator = QuadAllocatorSRV;
			PassParameters->VS.LumenCardScene = LumenSceneData.UniformBuffer;
			PassParameters->VS.NumClipmaps = ClipmapsToUpdate.Num();
			PassParameters->VS.GridResolution = VoxelMaskGridResolution;
			PassParameters->VS.TilesPerInstance = NumLumenQuadsInBuffer;
			PassParameters->VS.SceneObjectBounds = MeshSDFTracingParameters.SceneObjectBounds;
			PassParameters->VS.SceneObjectData = MeshSDFTracingParameters.SceneObjectData;

			PassParameters->PS.NumClipmaps = ClipmapsToUpdate.Num();
			PassParameters->PS.GridResolution = VoxelMaskGridResolution;
			GetLumenCardTracingParameters(View, TracingInputs, PassParameters->PS.TracingParameters, true);
			PassParameters->PS.MeshSDFTracingParameters = MeshSDFTracingParameters;
			PassParameters->PS.RWVoxelMask = VoxelMaskUAV;

			int32 CompactedClipmapIndex = 0;

			for (int32 ClipmapIndex : ClipmapsToUpdate)
			{
				FVoxelLightingClipmap Clipmap;
				const FVector LumenSceneCameraOrigin = GetLumenSceneViewOrigin(View, ClipmapIndex);
				ComputeVoxelLightingClipmap(Clipmap, LumenSceneCameraOrigin, ClipmapIndex, VoxelMaskGridResolution);

				PassParameters->VS.ClipmapWorldToUVScale[CompactedClipmapIndex] = TracingInputs.ClipmapWorldToUVScale[ClipmapIndex];
				PassParameters->VS.ClipmapWorldToUVBias[CompactedClipmapIndex] = TracingInputs.ClipmapWorldToUVBias[ClipmapIndex];
				PassParameters->VS.ClipmapWorldMin[CompactedClipmapIndex] = Clipmap.WorldMin;
				PassParameters->VS.ClipmapWorldSize[CompactedClipmapIndex] = Clipmap.WorldExtent * 2;
				PassParameters->VS.ClipmapVoxelSizeAndRadius[CompactedClipmapIndex] = Clipmap.GetVoxelSizeAndRadius();

				PassParameters->PS.ClipmapWorldMin[CompactedClipmapIndex] = Clipmap.WorldMin;
				PassParameters->PS.ClipmapWorldSize[CompactedClipmapIndex] = Clipmap.WorldExtent * 2;
				PassParameters->PS.TracingParameters.ClipmapVoxelSizeAndRadius[CompactedClipmapIndex] = Clipmap.GetVoxelSizeAndRadius();
				PassParameters->PS.TracingParameters.ClipmapWorldToUVScale[CompactedClipmapIndex] = TracingInputs.ClipmapWorldToUVScale[ClipmapIndex];
				PassParameters->PS.TracingParameters.ClipmapWorldToUVBias[CompactedClipmapIndex] = TracingInputs.ClipmapWorldToUVBias[ClipmapIndex];
				PassParameters->PS.TracingParameters.ClipmapWorldCenter[CompactedClipmapIndex] = TracingInputs.ClipmapWorldCenter[ClipmapIndex];
				PassParameters->PS.TracingParameters.ClipmapWorldExtent[CompactedClipmapIndex] = TracingInputs.ClipmapWorldExtent[ClipmapIndex];
				PassParameters->PS.TracingParameters.ClipmapWorldSamplingExtent[CompactedClipmapIndex] = TracingInputs.ClipmapWorldSamplingExtent[ClipmapIndex];
				CompactedClipmapIndex++;
			}

			PassParameters->CardIndirectArgs = CardIndirectArgsBuffer;

			FCardVoxelizeVS::FPermutationDomain PermutationVectorVS;
			PermutationVectorVS.Set<FCardVoxelizeVS::FTraceMeshSDF>(GLumenSceneVoxelLightingTraceMeshSDF != 0);
			auto VertexShader = View.ShaderMap->GetShader<FCardVoxelizeVS>(PermutationVectorVS);

			FCardVoxelizeMaskSetupPS::FPermutationDomain PermutationVectorPS;
			PermutationVectorPS.Set<FCardVoxelizeMaskSetupPS::FTraceMeshSDF>(GLumenSceneVoxelLightingTraceMeshSDF != 0);
			auto PixelShader = View.ShaderMap->GetShader<FCardVoxelizeMaskSetupPS>(PermutationVectorPS);

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("ScatterCardsToMask"),
				PassParameters,
				ERDGPassFlags::Raster,
				[VoxelMaskGridResolution, VertexShader, PixelShader, PassParameters](FRHICommandListImmediate& RHICmdList)
			{
				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

				RHICmdList.SetViewport(0, 0, 0.0f, VoxelMaskGridResolution.X, VoxelMaskGridResolution.Y, 1.0f);

				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
				GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();

				GraphicsPSOInit.PrimitiveType = UseRectTopologyForLumen() ? PT_RectList : PT_TriangleList;

				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GTileVertexDeclaration.VertexDeclarationRHI;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

				SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), PassParameters->VS);
				SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PassParameters->PS);

				RHICmdList.SetStreamSource(0, GLumenTileTexCoordVertexBuffer.VertexBufferRHI, 0);

				if (UseRectTopologyForLumen())
				{
					RHICmdList.DrawPrimitiveIndirect(PassParameters->CardIndirectArgs->GetIndirectRHICallBuffer(), 0);
				}
				else
				{
					RHICmdList.DrawIndexedPrimitiveIndirect(GLumenTileIndexBuffer.IndexBufferRHI, PassParameters->CardIndirectArgs->GetIndirectRHICallBuffer(), 0);
				}
			});
		}

		FPooledRenderTargetDesc LightingOITDesc(FPooledRenderTargetDesc::CreateVolumeDesc(VolumeTextureResolution.X * 4, VolumeTextureResolution.Y, VolumeTextureResolution.Z, PF_R32_UINT, FClearValueBinding::Transparent, TexCreate_None, TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV, false));
		VoxelOITLighting = GraphBuilder.CreateTexture(LightingOITDesc, TEXT("VoxelOITLighting"));

		FPooledRenderTargetDesc TransparencyOITDesc(FPooledRenderTargetDesc::CreateVolumeDesc(VolumeTextureResolution.X, VolumeTextureResolution.Y, VolumeTextureResolution.Z, PF_R32_UINT, FClearValueBinding::Transparent, TexCreate_None, TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV, false));
		VoxelOITTransparency = GraphBuilder.CreateTexture(TransparencyOITDesc, TEXT("VoxelOITTransparency"));

		VoxelOITLightingUAV = GraphBuilder.CreateUAV(VoxelOITLighting);
		VoxelOITTransparencyUAV = GraphBuilder.CreateUAV(VoxelOITTransparency);

		{
			FClearVoxelLightingCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FClearVoxelLightingCS::FParameters>();
			PassParameters->RWVoxelOITLighting = VoxelOITLightingUAV;
			PassParameters->RWVoxelOITTransparency = VoxelOITTransparencyUAV;
			PassParameters->VoxelMask = VoxelMask;
			PassParameters->VoxelMaskResolutionShift = GLumenSceneVoxelLightingMaskDownsampleShift;

			auto ComputeShader = View.ShaderMap->GetShader<FClearVoxelLightingCS>();
			const FIntVector GroupSize = FComputeShaderUtils::GetGroupCount(VolumeTextureResolution, ComputeVoxelLightingGroupSize);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("ClearVoxelLighting"),
				ComputeShader,
				PassParameters,
				GroupSize);
		}
	}

	if (bUseComputeScatter)
	{
		FComputeScatterCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FComputeScatterCS::FParameters>();
		GetLumenCardTracingParameters(View, TracingInputs, PassParameters->TracingParameters, true);
		PassParameters->MeshSDFTracingParameters = MeshSDFTracingParameters;
		PassParameters->RWVoxelVisBuffer = VoxelVisBufferUAV;
		PassParameters->QuadAllocator = QuadAllocatorSRV;
		PassParameters->QuadData = QuadDataSRV;
		PassParameters->GridResolution = ClipmapGridResolution;
		PassParameters->ComputeScatterIndirectArgsBuffer = ComputeScatterIndirectArgsBuffer;
		PassParameters->VoxelRayTracing = Lumen::UseVoxelRayTracing();

		int32 CompactedClipmapIndex = 0;

		for (int32 ClipmapIndex : ClipmapsToUpdate)
		{
			FVoxelLightingClipmap Clipmap;
			const FVector LumenSceneCameraOrigin = GetLumenSceneViewOrigin(View, ClipmapIndex);
			ComputeVoxelLightingClipmap(Clipmap, LumenSceneCameraOrigin, ClipmapIndex, VoxelGridResolution);

			PassParameters->ClipmapWorldMin[CompactedClipmapIndex] = Clipmap.WorldMin;
			PassParameters->ClipmapWorldSize[CompactedClipmapIndex] = Clipmap.WorldExtent * 2;
			PassParameters->ClipmapToGridScale[CompactedClipmapIndex] = Clipmap.ToGridScale;
			PassParameters->ClipmapToGridBias[CompactedClipmapIndex] = Clipmap.ToGridBias;

			PassParameters->TracingParameters.ClipmapWorldToUVScale[CompactedClipmapIndex] = TracingInputs.ClipmapWorldToUVScale[ClipmapIndex];
			PassParameters->TracingParameters.ClipmapWorldToUVBias[CompactedClipmapIndex] = TracingInputs.ClipmapWorldToUVBias[ClipmapIndex];
			PassParameters->TracingParameters.ClipmapVoxelSizeAndRadius[CompactedClipmapIndex] = TracingInputs.ClipmapVoxelSizeAndRadius[ClipmapIndex];
			PassParameters->TracingParameters.ClipmapWorldCenter[CompactedClipmapIndex] = TracingInputs.ClipmapWorldCenter[ClipmapIndex];
			PassParameters->TracingParameters.ClipmapWorldExtent[CompactedClipmapIndex] = TracingInputs.ClipmapWorldExtent[ClipmapIndex];
			PassParameters->TracingParameters.ClipmapWorldSamplingExtent[CompactedClipmapIndex] = TracingInputs.ClipmapWorldSamplingExtent[ClipmapIndex];

			CompactedClipmapIndex++;
		}

		FComputeScatterCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FComputeScatterCS::FSingleClipmapToUpdate>(ClipmapsToUpdate.Num() == 1);
		auto ComputeShader = View.ShaderMap->GetShader<FComputeScatterCS>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("ComputeScatter"),
			ComputeShader,
			PassParameters,
			ComputeScatterIndirectArgsBuffer,
			0);
	}
	else
	{
		FCardVoxelize* PassParameters = GraphBuilder.AllocParameters<FCardVoxelize>();

		PassParameters->VS.QuadData = QuadDataSRV;
		PassParameters->VS.QuadAllocator = QuadAllocatorSRV;
		PassParameters->VS.LumenCardScene = LumenSceneData.UniformBuffer;
		PassParameters->VS.NumClipmaps = ClipmapsToUpdate.Num();
		PassParameters->VS.GridResolution = VoxelGridResolution;
		PassParameters->VS.TilesPerInstance = NumLumenQuadsInBuffer;
		PassParameters->VS.SceneObjectBounds = MeshSDFTracingParameters.SceneObjectBounds;
		PassParameters->VS.SceneObjectData = MeshSDFTracingParameters.SceneObjectData;
		
		PassParameters->PS.NumClipmaps = ClipmapsToUpdate.Num();
		PassParameters->PS.GridResolution = VoxelGridResolution;
		GetLumenCardTracingParameters(View, TracingInputs, PassParameters->PS.TracingParameters, true);
		PassParameters->PS.MeshSDFTracingParameters = MeshSDFTracingParameters;
		PassParameters->PS.RWVoxelOITLighting = VoxelOITLightingUAV;
		PassParameters->PS.RWVoxelOITTransparency = VoxelOITTransparencyUAV;
		PassParameters->PS.RWVoxelVisBuffer = VoxelVisBufferUAV;
		PassParameters->PS.VoxelMask = VoxelMask;
		PassParameters->PS.VoxelMaskResolutionShift = GLumenSceneVoxelLightingMaskDownsampleShift;
		PassParameters->PS.VoxelRayTracing = Lumen::UseVoxelRayTracing();

		int32 CompactedClipmapIndex = 0;

		for (int32 ClipmapIndex : ClipmapsToUpdate)
		{
			FVoxelLightingClipmap Clipmap;
			const FVector LumenSceneCameraOrigin = GetLumenSceneViewOrigin(View, ClipmapIndex);
			ComputeVoxelLightingClipmap(Clipmap, LumenSceneCameraOrigin, ClipmapIndex, VoxelGridResolution);

			PassParameters->VS.ClipmapWorldToUVScale[CompactedClipmapIndex] = TracingInputs.ClipmapWorldToUVScale[ClipmapIndex];
			PassParameters->VS.ClipmapWorldToUVBias[CompactedClipmapIndex] = TracingInputs.ClipmapWorldToUVBias[ClipmapIndex];
			PassParameters->VS.ClipmapWorldMin[CompactedClipmapIndex] = Clipmap.WorldMin;
			PassParameters->VS.ClipmapWorldSize[CompactedClipmapIndex] = Clipmap.WorldExtent * 2;
			PassParameters->VS.ClipmapVoxelSizeAndRadius[CompactedClipmapIndex] = Clipmap.GetVoxelSizeAndRadius();

			PassParameters->PS.ClipmapWorldMin[CompactedClipmapIndex] = Clipmap.WorldMin;
			PassParameters->PS.ClipmapWorldSize[CompactedClipmapIndex] = Clipmap.WorldExtent * 2;
			PassParameters->PS.TracingParameters.ClipmapWorldToUVScale[CompactedClipmapIndex] = TracingInputs.ClipmapWorldToUVScale[ClipmapIndex];
			PassParameters->PS.TracingParameters.ClipmapWorldToUVBias[CompactedClipmapIndex] = TracingInputs.ClipmapWorldToUVBias[ClipmapIndex];
			PassParameters->PS.TracingParameters.ClipmapVoxelSizeAndRadius[CompactedClipmapIndex] = TracingInputs.ClipmapVoxelSizeAndRadius[ClipmapIndex];
			PassParameters->PS.TracingParameters.ClipmapWorldCenter[CompactedClipmapIndex] = TracingInputs.ClipmapWorldCenter[ClipmapIndex];
			PassParameters->PS.TracingParameters.ClipmapWorldExtent[CompactedClipmapIndex] = TracingInputs.ClipmapWorldExtent[ClipmapIndex];
			PassParameters->PS.TracingParameters.ClipmapWorldSamplingExtent[CompactedClipmapIndex] = TracingInputs.ClipmapWorldSamplingExtent[ClipmapIndex];
			CompactedClipmapIndex++;
		}

		PassParameters->CardIndirectArgs = CardIndirectArgsBuffer;

		FCardVoxelizeVS::FPermutationDomain PermutationVectorVS;
		PermutationVectorVS.Set<FCardVoxelizeVS::FTraceMeshSDF>(GLumenSceneVoxelLightingTraceMeshSDF != 0);
		auto VertexShader = View.ShaderMap->GetShader<FCardVoxelizeVS>(PermutationVectorVS);
		
		FCardVoxelizePS::FPermutationDomain PermutationVectorPS;
		PermutationVectorPS.Set<FCardVoxelizePS::FTraceMeshSDF>(GLumenSceneVoxelLightingTraceMeshSDF != 0);
		PermutationVectorPS.Set<FCardVoxelizePS::FCubeMapTree>(GLumenSceneVoxelLightingCubeMapTree != 0);
		PermutationVectorPS.Set<FCardVoxelizePS::FVoxelVisBuffer>(bUseVoxelVisBuffer);
		PermutationVectorPS = FCardVoxelizePS::RemapPermutation(PermutationVectorPS);
		auto PixelShader = View.ShaderMap->GetShader<FCardVoxelizePS>(PermutationVectorPS);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("ScatterCards"),
			PassParameters,
			ERDGPassFlags::Raster,
			[VoxelGridResolution, VertexShader, PixelShader, PassParameters](FRHICommandListImmediate& RHICmdList)
		{
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

			RHICmdList.SetViewport(0, 0, 0.0f, VoxelGridResolution.X, VoxelGridResolution.Y, 1.0f);

			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();

			GraphicsPSOInit.PrimitiveType = UseRectTopologyForLumen() ? PT_RectList : PT_TriangleList;

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GTileVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), PassParameters->VS);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PassParameters->PS);

			RHICmdList.SetStreamSource(0, GLumenTileTexCoordVertexBuffer.VertexBufferRHI, 0);

			if (UseRectTopologyForLumen())
			{
				RHICmdList.DrawPrimitiveIndirect(PassParameters->CardIndirectArgs->GetIndirectRHICallBuffer(), 0);
			}
			else
			{
				RHICmdList.DrawIndexedPrimitiveIndirect(GLumenTileIndexBuffer.IndexBufferRHI, PassParameters->CardIndirectArgs->GetIndirectRHICallBuffer(), 0);
			}
		});
	}

	FRDGTextureUAVRef VoxelLightingUAV = GraphBuilder.CreateUAV(VoxelLighting, ERDGChildResourceFlags::NoUAVBarrier);

	FIntVector ClipmapTextureResolution = VolumeTextureResolution;
	ClipmapTextureResolution.Y /= ClipmapsToUpdate.Num();

	int32 CompactedClipmapIndex = 0;

	if (bUseVoxelVisBuffer)
	{
		for (int32 ClipmapIndex : ClipmapsToUpdate)
		{
			// Run one lane per voxel direction (3 * 2 = NUM_VOXEL_DIRECTIONS)
			FIntVector OutputGridResolution = ClipmapGridResolution;
			OutputGridResolution.X *= 3;
			OutputGridResolution.Y *= 2;

			FVoxelVisBufferShadingCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVoxelVisBufferShadingCS::FParameters>();
			PassParameters->RWVoxelLighting = VoxelLightingUAV;
			GetLumenCardTracingParameters(View, TracingInputs, PassParameters->TracingParameters, true);
			PassParameters->MeshSDFTracingParameters = MeshSDFTracingParameters;
			PassParameters->VoxelVisBuffer = VoxelVisBuffer;
			PassParameters->SourceClipmapIndex = CompactedClipmapIndex;
			PassParameters->TargetClipmapIndex = ClipmapIndex;
			PassParameters->ClipmapGridResolution = ClipmapGridResolution;
			PassParameters->OutputGridResolution = OutputGridResolution;
			PassParameters->VoxelRayTracing = Lumen::UseVoxelRayTracing();

			FVoxelLightingClipmap Clipmap;
			const FVector LumenSceneCameraOrigin = GetLumenSceneViewOrigin(View, ClipmapIndex);
			ComputeVoxelLightingClipmap(Clipmap, LumenSceneCameraOrigin, ClipmapIndex, ClipmapGridResolution);
			PassParameters->GridMin = Clipmap.WorldMin;
			PassParameters->GridVoxelSize = Clipmap.VoxelSize;

			bool bDistantScene = false;
			if (GLumenSceneVoxelLightingDistantScene != 0 
				&& LumenSceneData.DistantCardIndices.Num() > 0
				&& ClipmapIndex + 1 == GetNumLumenVoxelClipmaps())
			{
				bDistantScene = true;
			}

			FVoxelVisBufferShadingCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FVoxelVisBufferShadingCS::FDistantScene>(bDistantScene);
			auto ComputeShader = View.ShaderMap->GetShader<FVoxelVisBufferShadingCS>(PermutationVector);

			const FIntVector GroupSize = FComputeShaderUtils::GetGroupCount(OutputGridResolution, FVoxelVisBufferShadingCS::GetGroupSize());

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("VoxelVisBufferShading %u", ClipmapIndex),
				ComputeShader,
				PassParameters,
				GroupSize);

			++CompactedClipmapIndex;
		}
	}
	else
	{
		for (int32 ClipmapIndex : ClipmapsToUpdate)
		{
			FCompactVoxelLightingCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCompactVoxelLightingCS::FParameters>();
			PassParameters->RWVoxelLighting = VoxelLightingUAV;

			PassParameters->VoxelOITLighting = VoxelOITLighting;
			PassParameters->VoxelOITTransparency = VoxelOITTransparency;
			PassParameters->VoxelMask = VoxelMask;

			PassParameters->GridResolution = VoxelGridResolution;
			PassParameters->ClipmapTextureResolution = ClipmapTextureResolution;
			PassParameters->VoxelMaskResolutionShift = GLumenSceneVoxelLightingMaskDownsampleShift;
			PassParameters->SourceClipmapIndex = CompactedClipmapIndex;
			PassParameters->DestClipmapIndex = ClipmapIndex;

			auto ComputeShader = View.ShaderMap->GetShader<FCompactVoxelLightingCS>();
			const FIntVector GroupSize = FComputeShaderUtils::GetGroupCount(ClipmapTextureResolution, ComputeVoxelLightingGroupSize);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("CompactVoxelLighting %u", ClipmapIndex),
				ComputeShader,
				PassParameters,
				GroupSize);

			CompactedClipmapIndex++;
		}
	}
}

bool ShouldUpdateVoxelClipmap(int32 ClipmapIndex, int32 NumClipmaps, uint32 FrameNumber)
{
	if (GLumenSceneVoxelLightingForceUpdateClipmapIndex >= 0 && GLumenSceneVoxelLightingForceUpdateClipmapIndex < NumClipmaps)
	{
		return ClipmapIndex == GLumenSceneVoxelLightingForceUpdateClipmapIndex;
	}

	if (NumClipmaps == 1)
	{
		return true;
	}
	else if (ClipmapIndex == 0)
	{
		return FrameNumber % 2 == 0;
	}
	else if (ClipmapIndex == 1)
	{
		return FrameNumber % 8 == 1 || FrameNumber % 8 == 5;
	}
	else if (ClipmapIndex == 2)
	{
		return FrameNumber % 8 == 3;
	}
	else if (NumClipmaps > 4)
	{
		if (ClipmapIndex == 3)
		{
			return FrameNumber % 16 == 7;
		}
		else
		{
			return FrameNumber % 16 == 15;
		}
	}
	else
	{
		if (ClipmapIndex == 3)
		{
			return FrameNumber % 8 == 7;
		}
		else
		{
			return FrameNumber % 8 == 1;
		}
	}
}

void FDeferredShadingSceneRenderer::ComputeLumenSceneVoxelLighting(
	FRDGBuilder& GraphBuilder,
	FLumenCardTracingInputs& TracingInputs,
	FGlobalShaderMap* GlobalShaderMap)
{
	LLM_SCOPE(ELLMTag::Lumen);

	const FViewInfo& View = Views[0];

	const int32 ClampedNumClipmapLevels = GetNumLumenVoxelClipmaps();

	FPooledRenderTargetDesc LightingDesc(FPooledRenderTargetDesc::CreateVolumeDesc(
		GetClipmapResolutionXY(), 
		GetClipmapResolutionXY() * ClampedNumClipmapLevels, 
		GetClipmapResolutionZ() * 6, 
		PF_FloatRGBA, 
		FClearValueBinding::Black, 
		TexCreate_None, 
		TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV | TexCreate_3DTiling,
		false));
	LightingDesc.AutoWritable = false;
	FRDGTextureRef VoxelLighting = TracingInputs.VoxelLighting;
	bool bForceFullUpdate = GLumenSceneVoxelLightingForceFullUpdate != 0;

	if (!VoxelLighting || !VoxelLighting->Desc.Compare(LightingDesc, true))
	{
		bForceFullUpdate = true;
		VoxelLighting = GraphBuilder.CreateTexture(LightingDesc, TEXT("VoxelLighting"));
	}

	TArray<int32, SceneRenderingAllocator> ClipmapsToUpdate;
	ClipmapsToUpdate.Empty(ClampedNumClipmapLevels);

	for (int32 ClipmapIndex = 0; ClipmapIndex < ClampedNumClipmapLevels; ClipmapIndex++)
	{
		if (bForceFullUpdate || ShouldUpdateVoxelClipmap(ClipmapIndex, ClampedNumClipmapLevels, View.ViewState->GetFrameIndex()))
		{
			ClipmapsToUpdate.Add(ClipmapIndex);
		}
	}

	ensureMsgf(bForceFullUpdate || ClipmapsToUpdate.Num() <= 1, TEXT("Tweak ShouldUpdateVoxelClipmap for better clipmap update distribution"));

	FString ClipmapsToUpdateString;

	for (int32 ToUpdateIndex = 0; ToUpdateIndex < ClipmapsToUpdate.Num(); ++ToUpdateIndex)
	{
		ClipmapsToUpdateString += FString::FromInt(ClipmapsToUpdate[ToUpdateIndex]);
		if (ToUpdateIndex + 1 < ClipmapsToUpdate.Num())
		{
			ClipmapsToUpdateString += TEXT(",");
		}
	}

	RDG_EVENT_SCOPE(GraphBuilder, "VoxelizeCards Clipmaps=[%s]", *ClipmapsToUpdateString);

	if (ClipmapsToUpdate.Num() > 0)
	{
		const FIntVector VoxelGridResolution = GetClipmapResolution();

		for (int32 ClipmapIndex : ClipmapsToUpdate)
		{
			FVoxelLightingClipmap Clipmap;
			const FVector LumenSceneCameraOrigin = GetLumenSceneViewOrigin(View, ClipmapIndex);
			ComputeVoxelLightingClipmap(Clipmap, LumenSceneCameraOrigin, ClipmapIndex, VoxelGridResolution);

			TracingInputs.ClipmapWorldToUVScale[ClipmapIndex] = FVector(1.0f, 1.0f, 1.0f) / (2 * Clipmap.WorldExtent);
			TracingInputs.ClipmapWorldToUVBias[ClipmapIndex] = -Clipmap.WorldMin * TracingInputs.ClipmapWorldToUVScale[ClipmapIndex];
			TracingInputs.ClipmapWorldCenter[ClipmapIndex] = Clipmap.WorldMin + Clipmap.WorldExtent;
			TracingInputs.ClipmapWorldExtent[ClipmapIndex] = Clipmap.WorldExtent;
			TracingInputs.ClipmapWorldSamplingExtent[ClipmapIndex] = Clipmap.WorldExtent - 0.5f * Clipmap.VoxelSize;
			TracingInputs.ClipmapVoxelSizeAndRadius[ClipmapIndex] = Clipmap.GetVoxelSizeAndRadius();
		}

		if (GLumenSceneVoxelLightingRasterizerScatter)
		{
			InjectCardsWithRasterizerScatter(View, Scene, TracingInputs, VoxelLighting, ClipmapsToUpdate, GraphBuilder);
		}
		else
		{
			InjectCardsWithComputeGather(View, TracingInputs, VoxelLighting, ClipmapsToUpdate, GraphBuilder);
		}

		extern int32 GLumenRadiosityMergedVoxelDirections;
		FRDGTextureRef MergedVoxelLighting = TracingInputs.MergedVoxelLighting;

		if (GLumenRadiosityMergedVoxelDirections)
		{
			FPooledRenderTargetDesc MergedLightingDesc(FPooledRenderTargetDesc::CreateVolumeDesc(
				GetClipmapResolutionXY(),
				GetClipmapResolutionXY() * ClampedNumClipmapLevels,
				GetClipmapResolutionZ() * 8,
				PF_FloatRGBA,
				FClearValueBinding::Black,
				TexCreate_None,
				TexCreate_ShaderResource | TexCreate_UAV | TexCreate_3DTiling,
				false));

			if (!MergedVoxelLighting || !MergedVoxelLighting->Desc.Compare(MergedLightingDesc, true))
			{
				MergedVoxelLighting = GraphBuilder.CreateTexture(MergedLightingDesc, TEXT("MergedVoxelLighting"));
			}

			FRDGTextureUAVRef MergedVoxelLightingUAV = GraphBuilder.CreateUAV(MergedVoxelLighting);

			for (int32 ClipmapIndex : ClipmapsToUpdate)
			{
				FMergeVoxelLightingCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FMergeVoxelLightingCS::FParameters>();
				PassParameters->RWMergedVoxelLighting = MergedVoxelLightingUAV;

				PassParameters->FaceVoxelLighting = VoxelLighting;
				PassParameters->TargetClipmapIndex = ClipmapIndex;
				PassParameters->GridResolution = VoxelGridResolution;

				auto ComputeShader = View.ShaderMap->GetShader<FMergeVoxelLightingCS>();
				const FIntVector GroupSize = FComputeShaderUtils::GetGroupCount(PassParameters->GridResolution, ComputeVoxelLightingGroupSize);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("MergeVoxelLighting"),
					ComputeShader,
					PassParameters,
					GroupSize);
			}
		}

		TracingInputs.VoxelLighting = VoxelLighting;
		TracingInputs.MergedVoxelLighting = MergedVoxelLighting;
		TracingInputs.VoxelGridResolution = VoxelGridResolution;
		TracingInputs.NumClipmapLevels = ClampedNumClipmapLevels;

		Lumen::UpdateVoxelDistanceField(GraphBuilder,
			View,
			ClipmapsToUpdate,
			TracingInputs);
	}
}
