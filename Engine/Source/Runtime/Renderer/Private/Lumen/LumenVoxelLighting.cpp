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
#include "DistanceFieldLightingShared.h"
#include "LumenMeshCards.h"
#include "GlobalDistanceField.h"
#include "LumenTracingUtils.h"

int32 GLumenSceneClipmapResolution = 64;
FAutoConsoleVariableRef CVarLumenSceneClipmapResolution(
	TEXT("r.LumenScene.VoxelLighting.ClipmapResolution"),
	GLumenSceneClipmapResolution,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GLumenSceneClipmapZResolutionDivisor = 1;
FAutoConsoleVariableRef CVarLumenSceneClipmapZResolutionDivisor(
	TEXT("r.LumenScene.VoxelLighting.ClipmapZResolutionDivisor"),
	GLumenSceneClipmapZResolutionDivisor,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GLumenSceneNumClipmapLevels = 4;
FAutoConsoleVariableRef CVarLumenSceneNumClipmapLevels(
	TEXT("r.LumenScene.VoxelLighting.NumClipmapLevels"),
	GLumenSceneNumClipmapLevels,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GLumenSceneFirstClipmapWorldExtent = 2500.0f;
FAutoConsoleVariableRef CVarLumenSceneClipmapWorldExtent(
	TEXT("r.LumenScene.VoxelLighting.ClipmapWorldExtent"),
	GLumenSceneFirstClipmapWorldExtent,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GLumenSceneVoxelLightingAverageObjectsPerVisBufferTile = 128;
FAutoConsoleVariableRef CVarLumenSceneVoxelLightingAverageObjectsPerVisBufferTile(
	TEXT("r.LumenScene.VoxelLighting.AverageObjectsPerVisBufferTile"),
	GLumenSceneVoxelLightingAverageObjectsPerVisBufferTile,
	TEXT("Average expected number of objects per vis buffer tile, used to preallocate memory for the cull grid."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenSceneVoxelLightingDistantScene = 1;
FAutoConsoleVariableRef CVarLumenSceneVoxelLightingDistantScene(
	TEXT("r.LumenScene.VoxelLighting.DistantScene"),
	GLumenSceneVoxelLightingDistantScene,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenSceneVoxelLightingMeshSDFRadiusThresholdFactor = 0.5f;
FAutoConsoleVariableRef CVarLumenSceneVoxelLightingMeshSDFRadiusThresholdFactor(
	TEXT("r.LumenScene.VoxelLighting.MeshSDFRadiusThresholdFactor"),
	GLumenSceneVoxelLightingMeshSDFRadiusThresholdFactor,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenSceneVoxelLightingMaskDownsampleShift = 2;
FAutoConsoleVariableRef CVarLumenSceneVoxelLightingMaskDownsampleShift(
	TEXT("r.LumenScene.VoxelLighting.MaskDownsampleShift"),
	GLumenSceneVoxelLightingMaskDownsampleShift,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenSceneVoxelLightingReset = 0;
FAutoConsoleVariableRef CVarLumenSceneVoxelLightingReset(
	TEXT("r.LumenScene.VoxelLighting.Reset"),
	GLumenSceneVoxelLightingReset,
	TEXT("Reset all voxel lighting.\n"),
	ECVF_RenderThreadSafe
);

int32 GLumenSceneVoxelLightingForceFullUpdate = 0;
FAutoConsoleVariableRef CVarLumenSceneVoxelLightingForceFullUpdate(
	TEXT("r.LumenScene.VoxelLighting.ForceFullUpdate"),
	GLumenSceneVoxelLightingForceFullUpdate,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenSceneVoxelLightingForceUpdateClipmapIndex = -1;
FAutoConsoleVariableRef CVarLumenSceneVoxelLightingForceClipmapIndex(
	TEXT("r.LumenScene.VoxelLighting.ForceUpdateClipmapIndex"),
	GLumenSceneVoxelLightingForceUpdateClipmapIndex,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenSceneVoxelLightingForceMovementUpdate = 0;
FAutoConsoleVariableRef CVarLumenSceneVoxelLightingForceMovementUpdate(
	TEXT("r.LumenScene.VoxelLighting.ForceMovementUpdate"),
	GLumenSceneVoxelLightingForceMovementUpdate,
	TEXT("Whether to force N texel border on X, Y and Z update each frame."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

constexpr uint32 GNumVoxelDirections = 6;
constexpr uint32 GVisBufferTileSize = 4;

void Lumen::DebugResetVoxelLighting()
{
	GLumenSceneVoxelLightingReset = 1;
}

bool Lumen::UseVoxelLighting(const FSceneViewFamily& ViewFamily)
{
	if (!Lumen::IsSoftwareRayTracingSupported())
	{
		return false;
	}

	// All features use Hardware RayTracing, no need to update voxel lighting
	if (Lumen::UseHardwareRayTracedSceneLighting(ViewFamily)
		&& Lumen::UseHardwareRayTracedScreenProbeGather(ViewFamily)
		&& Lumen::UseHardwareRayTracedReflections(ViewFamily)
		&& Lumen::UseHardwareRayTracedRadianceCache(ViewFamily)
		&& Lumen::UseHardwareRayTracedTranslucencyVolume(ViewFamily)
		&& Lumen::UseHardwareRayTracedVisualize(ViewFamily))
	{
		return false;
	}

	return true;
}

float Lumen::GetFirstClipmapWorldExtent()
{
	return FMath::Max(GLumenSceneFirstClipmapWorldExtent, 1.0f);
}

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

FIntVector GetUpdateGridResolution()
{
	const FIntVector ClipmapResolution = GetClipmapResolution();
	return FIntVector::DivideAndRoundUp(ClipmapResolution, GVisBufferTileSize);
}

FVector GetLumenVoxelClipmapExtent(int32 ClipmapIndex)
{
	const FVector FirstClipmapWorldExtent(Lumen::GetFirstClipmapWorldExtent(), Lumen::GetFirstClipmapWorldExtent(), Lumen::GetFirstClipmapWorldExtent() / GLumenSceneClipmapZResolutionDivisor);
	const float ClipmapWorldScale = (float)(1 << ClipmapIndex);
	return FirstClipmapWorldExtent * ClipmapWorldScale;
}

int32 GetNumLumenVoxelClipmaps(float LumenSceneViewDistance)
{
	int32 WantedClipmaps = GLumenSceneNumClipmapLevels;

	if (GetLumenVoxelClipmapExtent(WantedClipmaps + 1).X <= LumenSceneViewDistance)
	{
		WantedClipmaps += 2;
	}
	else if (GetLumenVoxelClipmapExtent(WantedClipmaps).X <= LumenSceneViewDistance)
	{
		WantedClipmaps += 1;
	}

	if (GLumenFastCameraMode != 0 && GLumenDistantScene == 0)
	{
		WantedClipmaps++;
	}

	return FMath::Clamp(WantedClipmaps, 1, MaxVoxelClipmapLevels);
}

FVector GetLumenSceneViewOrigin(const FViewInfo& View, int32 ClipmapIndex)
{
	FVector CameraOrigin = View.ViewMatrices.GetViewOrigin();

	if (View.ViewState)
	{
		FVector CameraVelocityOffset = View.ViewState->GlobalDistanceFieldData->CameraVelocityOffset;

		if (ClipmapIndex > 0)
		{
			const FVector ClipmapExtent = GetLumenVoxelClipmapExtent(ClipmapIndex);
			const float MaxCameraDriftFraction = .75f;
			CameraVelocityOffset.X = FMath::Clamp<float>(CameraVelocityOffset.X, -ClipmapExtent.X * MaxCameraDriftFraction, ClipmapExtent.X * MaxCameraDriftFraction);
			CameraVelocityOffset.Y = FMath::Clamp<float>(CameraVelocityOffset.Y, -ClipmapExtent.Y * MaxCameraDriftFraction, ClipmapExtent.Y * MaxCameraDriftFraction);
			CameraVelocityOffset.Z = FMath::Clamp<float>(CameraVelocityOffset.Z, -ClipmapExtent.Z * MaxCameraDriftFraction, ClipmapExtent.Z * MaxCameraDriftFraction);
		}

		CameraOrigin += CameraVelocityOffset;
	}

	// Frozen camera
	if (View.ViewState)
	{
		if (Lumen::ShouldUpdateLumenSceneViewOrigin())
		{
			View.ViewState->GlobalDistanceFieldData->bUpdateViewOrigin = true;
		}
		else
		{
			if (View.ViewState->GlobalDistanceFieldData->bUpdateViewOrigin)
			{
				View.ViewState->GlobalDistanceFieldData->LastViewOrigin = View.ViewMatrices.GetViewOrigin();
				View.ViewState->GlobalDistanceFieldData->bUpdateViewOrigin = false;
			}
		}

		if (!View.ViewState->GlobalDistanceFieldData->bUpdateViewOrigin)
		{
			CameraOrigin = View.ViewState->GlobalDistanceFieldData->LastViewOrigin;
		}
	}

	return CameraOrigin;
}

FIntVector AlignVisBufferSizeToTileSize(FIntVector Dimension)
{
	FIntVector Groups = FIntVector::DivideAndRoundUp(Dimension, GVisBufferTileSize);
	return Groups * GVisBufferTileSize;
}

class FClearVoxelLightingClipmapCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClearVoxelLightingClipmapCS)
	SHADER_USE_PARAMETER_STRUCT(FClearVoxelLightingClipmapCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, RWVoxelLighting)
		SHADER_PARAMETER(uint32, ClipmapIndex)
		SHADER_PARAMETER(FIntVector, ClipmapGridResolution)
		SHADER_PARAMETER(FIntVector, OutputGridResolution)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static FIntVector GetGroupSize()
	{
		return FIntVector(4, 4, 4);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize().X);
	}
};

IMPLEMENT_GLOBAL_SHADER(FClearVoxelLightingClipmapCS, "/Engine/Private/Lumen/LumenVoxelLighting.usf", "ClearVoxelLightingClipmapCS", SF_Compute);

class FClearIndirectAgrBuffersCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClearIndirectAgrBuffersCS);
	SHADER_USE_PARAMETER_STRUCT(FClearIndirectAgrBuffersCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWClearVisBufferIndirectArgBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWTraceSetupIndirectArgBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWTraceIndirectArgBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static int32 GetGroupSize()
	{
		return 1;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FClearIndirectAgrBuffersCS, "/Engine/Private/Lumen/LumenVoxelLighting.usf", "ClearIndirectArgBuffersCS", SF_Compute);

class FBuildUpdateGridTilesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FBuildUpdateGridTilesCS);
	SHADER_USE_PARAMETER_STRUCT(FBuildUpdateGridTilesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWGridTileBuffer)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<uint>, RWGridTileMaskTexture)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWClearVisBufferIndirectArgBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, UpdateBoundsBuffer)
		SHADER_PARAMETER(uint32, NumUpdateBounds)
		SHADER_PARAMETER(FIntVector, GridResolution)
		SHADER_PARAMETER(FVector3f, GridCoordToWorldCenterScale)
		SHADER_PARAMETER(FVector3f, GridCoordToWorldCenterBias)
		SHADER_PARAMETER(FVector3f, TileWorldExtent)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static int32 GetGroupSize()
	{
		return 64;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FBuildUpdateGridTilesCS, "/Engine/Private/Lumen/LumenVoxelLighting.usf", "BuildUpdateGridTilesCS", SF_Compute);

class FClearVisBuffer : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClearVisBuffer);
	SHADER_USE_PARAMETER_STRUCT(FClearVisBuffer, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWVoxelVisBuffer)
		RDG_BUFFER_ACCESS(ClearVisBufferIndirectArgBuffer, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, UpdateTileBuffer)
		SHADER_PARAMETER(FIntVector, ClipmapGridResolution)
		SHADER_PARAMETER(FVector3f, VoxelCoordToUVScale)
		SHADER_PARAMETER(FVector3f, VoxelCoordToUVBias)
		SHADER_PARAMETER(uint32, ClipmapIndex)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static FIntVector GetGroupSize()
	{
		return FIntVector(4 * GNumVoxelDirections, 4, 4);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

IMPLEMENT_GLOBAL_SHADER(FClearVisBuffer, "/Engine/Private/Lumen/LumenVoxelLighting.usf", "ClearVisBufferCS", SF_Compute);

enum class EMeshType
{
	SDF,
	Heightfield,

	MAX
};

class FCullToVoxelClipmapCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCullToVoxelClipmapCS);
	SHADER_USE_PARAMETER_STRUCT(FCullToVoxelClipmapCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWObjectIndexBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWTraceSetupIndirectArgBuffer)
		// SDF parameters
		SHADER_PARAMETER_STRUCT_INCLUDE(FDistanceFieldObjectBufferParameters, DistanceFieldObjectBuffers)
		// Heightfield parameters
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)

		SHADER_PARAMETER(FVector3f, VoxelClipmapWorldCenter)
		SHADER_PARAMETER(FVector3f, VoxelClipmapWorldExtent)
		SHADER_PARAMETER(float, MeshRadiusThreshold)
	END_SHADER_PARAMETER_STRUCT()

	class FMeshTypeDim : SHADER_PERMUTATION_ENUM_CLASS("MESH_TYPE", EMeshType);
	using FPermutationDomain = TShaderPermutationDomain<FMeshTypeDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static int32 GetGroupSize()
	{
		return 64;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FCullToVoxelClipmapCS, "/Engine/Private/Lumen/LumenVoxelLighting.usf", "CullToVoxelClipmapCS", SF_Compute);

class FSetupVoxelTracesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSetupVoxelTracesCS);
	SHADER_USE_PARAMETER_STRUCT(FSetupVoxelTracesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWTraceIndirectArgBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint2>, RWVoxelTraceData)
		RDG_BUFFER_ACCESS(TraceSetupIndirectArgBuffer, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<uint>, UpdateTileMaskTexture)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, ObjectIndexBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, ObjectIndexNumBuffer)
		// SDF parameters
		SHADER_PARAMETER_STRUCT_INCLUDE(FDistanceFieldObjectBufferParameters, DistanceFieldObjectBuffers)
		// Heightfield parameters
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)

		SHADER_PARAMETER(FVector3f, ConservativeRasterizationExtent)
		SHADER_PARAMETER(FIntVector, UpdateGridResolution)
		SHADER_PARAMETER(FVector3f, ClipmapToUpdateGridScale)
		SHADER_PARAMETER(FVector3f, ClipmapToUpdateGridBias)
	END_SHADER_PARAMETER_STRUCT()

	class FMeshTypeDim : SHADER_PERMUTATION_ENUM_CLASS("MESH_TYPE", EMeshType);
	using FPermutationDomain = TShaderPermutationDomain<FMeshTypeDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static FIntVector GetGroupSize()
	{
		return FIntVector(4, 4, 4);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize().X);
	}
};

IMPLEMENT_GLOBAL_SHADER(FSetupVoxelTracesCS, "/Engine/Private/Lumen/LumenVoxelLighting.usf", "SetupVoxelTracesCS", SF_Compute);

class FVoxelTraceCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVoxelTraceCS)
	SHADER_USE_PARAMETER_STRUCT(FVoxelTraceCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWVoxelVisBuffer)
		RDG_BUFFER_ACCESS(TraceIndirectArgBuffer, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VoxelTraceAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint2>, VoxelTraceData)
		SHADER_PARAMETER(FVector3f, GridMin)
		SHADER_PARAMETER(FVector3f, GridVoxelSize)
		SHADER_PARAMETER(FIntVector, ClipmapGridResolution)
		SHADER_PARAMETER(FIntVector, OutputGridResolution)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenMeshSDFTracingParameters, MeshSDFTracingParameters)
		SHADER_PARAMETER(FIntVector, CullGridResolution)
		SHADER_PARAMETER(FVector3f, VoxelCoordToUVScale)
		SHADER_PARAMETER(FVector3f, VoxelCoordToUVBias)
		SHADER_PARAMETER(uint32, ClipmapIndex)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static FIntVector GetGroupSize()
	{
		return FIntVector(64, 1, 1);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize().X);
	}
};

IMPLEMENT_GLOBAL_SHADER(FVoxelTraceCS, "/Engine/Private/Lumen/LumenVoxelLighting.usf", "VoxelTraceCS", SF_Compute);

class FHeightfieldVoxelTraceCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHeightfieldVoxelTraceCS)
	SHADER_USE_PARAMETER_STRUCT(FHeightfieldVoxelTraceCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWVoxelVisBuffer)
		RDG_BUFFER_ACCESS(TraceIndirectArgBuffer, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VoxelTraceAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint2>, VoxelTraceData)
		SHADER_PARAMETER(FVector3f, GridMin)
		SHADER_PARAMETER(FVector3f, GridVoxelSize)
		SHADER_PARAMETER(FIntVector, ClipmapGridResolution)
		SHADER_PARAMETER(FIntVector, OutputGridResolution)
		SHADER_PARAMETER(FIntVector, CullGridResolution)
		SHADER_PARAMETER(FVector3f, VoxelCoordToUVScale)
		SHADER_PARAMETER(FVector3f, VoxelCoordToUVBias)
		SHADER_PARAMETER(uint32, ClipmapIndex)
		SHADER_PARAMETER(int32, HeightfieldMaxTracingSteps)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static FIntVector GetGroupSize()
	{
		return FIntVector(64, 1, 1);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize().X);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHeightfieldVoxelTraceCS, "/Engine/Private/Lumen/LumenVoxelLighting.usf", "HeightfieldVoxelTraceCS", SF_Compute);

class FCompactVisBufferCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCompactVisBufferCS)
	SHADER_USE_PARAMETER_STRUCT(FCompactVisBufferCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWCompactedVisBufferAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, RWCompactedVisBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VoxelVisBuffer)
		SHADER_PARAMETER(FIntVector, ClipmapGridResolution)
		SHADER_PARAMETER(uint32, ClipmapIndex)
		SHADER_PARAMETER(FVector3f, VoxelCoordToUVScale)
		SHADER_PARAMETER(FVector3f, VoxelCoordToUVBias)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static FIntVector GetGroupSize()
	{
		return FIntVector(4, 4, 4);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize().X);
	}
};

IMPLEMENT_GLOBAL_SHADER(FCompactVisBufferCS, "/Engine/Private/Lumen/LumenVoxelLighting.usf", "CompactVisBufferCS", SF_Compute);

class FSetupVisBufferShadingCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSetupVisBufferShadingCS)
	SHADER_USE_PARAMETER_STRUCT(FSetupVisBufferShadingCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCompactedVisBufferIndirectArguments)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CompactedVisBufferAllocator)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static int32 GetGroupSize()
	{
		return 1;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FSetupVisBufferShadingCS, "/Engine/Private/Lumen/LumenVoxelLighting.usf", "SetupVisBufferShadingCS", SF_Compute);

class FVisBufferShadingCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVisBufferShadingCS)
	SHADER_USE_PARAMETER_STRUCT(FVisBufferShadingCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, RWVoxelLighting)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenMeshSDFTracingParameters, MeshSDFTracingParameters)
		RDG_BUFFER_ACCESS(CompactedVisBufferIndirectArguments, ERHIAccess::IndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CompactedVisBufferAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CompactedVisBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VoxelVisBuffer)
		SHADER_PARAMETER(uint32, ClipmapIndex)
		SHADER_PARAMETER(FVector3f, GridMin)
		SHADER_PARAMETER(FVector3f, GridVoxelSize)
		SHADER_PARAMETER(FIntVector, ClipmapGridResolution)
		SHADER_PARAMETER(FVector3f, VoxelCoordToUVScale)
		SHADER_PARAMETER(FVector3f, VoxelCoordToUVBias)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	class FDistantScene : SHADER_PERMUTATION_BOOL("DISTANT_SCENE");
	class FShadeMeshSDFDim : SHADER_PERMUTATION_BOOL("SHADE_MESH_SDF");
	class FShadeHeightfieldDim : SHADER_PERMUTATION_BOOL("SHADE_HEIGHTFIELD");
	using FPermutationDomain = TShaderPermutationDomain<FDistantScene, FShadeMeshSDFDim, FShadeHeightfieldDim>;

	static int32 GetGroupSize()
	{
		return 64;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FVisBufferShadingCS, "/Engine/Private/Lumen/LumenVoxelLighting.usf", "VisBufferShadingCS", SF_Compute);

uint32 NumVoxelizedObjects(const FDistanceFieldSceneData& DistanceFieldSceneData, const FLumenSceneData& LumenSceneData)
{
	return DistanceFieldSceneData.NumObjectsInBuffer + LumenSceneData.Heightfields.Num();
}

void VoxelizeVisBuffer(
	const FViewInfo& View,
	FScene* Scene,
	const FLumenCardTracingInputs& TracingInputs,
	const FLumenViewCardTracingInputs& ViewTracingInputs,
	FRDGTextureRef VoxelLighting,
	FRDGBufferRef VoxelVisBuffer,
	const TArray<int32, SceneRenderingAllocator>& ClipmapsToUpdate,
	FRDGBuilder& GraphBuilder)
{
	const FDistanceFieldSceneData& DistanceFieldSceneData = Scene->DistanceFieldSceneData;
	const uint32 NumDistanceFieldObjects = DistanceFieldSceneData.NumObjectsInBuffer;
	const FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;

	if (NumVoxelizedObjects(DistanceFieldSceneData, LumenSceneData) == 0)
	{
		// Nothing to voxelize, just clear the entire volume and return
		const FLinearColor VoxelLightingClearValue(0.0f, 0.0f, 0.0f, 0.0f);
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(VoxelLighting), VoxelLightingClearValue);
		return;
	}

	const FIntVector VoxelGridResolution = GetClipmapResolution();
	const FIntVector ClipmapGridResolution = GetClipmapResolution();
	const FIntVector VolumeTextureResolution(GetClipmapResolutionXY(), GetClipmapResolutionXY() * ClipmapsToUpdate.Num(), GetClipmapResolutionZ() * GNumVoxelDirections);

	FRDGTextureUAVRef VoxelLightingUAV = GraphBuilder.CreateUAV(VoxelLighting, ERDGUnorderedAccessViewFlags::SkipBarrier);

	FLumenMeshSDFTracingParameters MeshSDFTracingParameters;
	SetupLumenMeshSDFTracingParameters(GraphBuilder, Scene, View, MeshSDFTracingParameters);

	// Vis buffer shading
	for (int32 ClipmapIndex : ClipmapsToUpdate)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "VoxelizeVisBuffer Clipmap:%d", ClipmapIndex);

		const FLumenVoxelLightingClipmapState& Clipmap = View.ViewState->Lumen.VoxelLightingClipmapState[ClipmapIndex];

		FRDGBufferRef CompactedVisBufferAllocator = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1), TEXT("Lumen.CompactedVisBufferAllocator"));
		FRDGBufferRef CompactedVisBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), ClipmapGridResolution.X * ClipmapGridResolution.Y * ClipmapGridResolution.Z * GNumVoxelDirections), TEXT("Lumen.CompactedVisBuffer"));
		FRDGBufferRef CompactedVisBufferIndirectArguments = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("Lumen.CompactedVisBufferIndirectArguments"));

		// Clear current voxel lighting clipmap
		{
			FIntVector OutputGridResolution = ClipmapGridResolution;
			OutputGridResolution.Z *= GNumVoxelDirections;

			FClearVoxelLightingClipmapCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FClearVoxelLightingClipmapCS::FParameters>();
			PassParameters->View = View.ViewUniformBuffer;
			PassParameters->RWVoxelLighting = VoxelLightingUAV;
			PassParameters->ClipmapIndex = ClipmapIndex;
			PassParameters->ClipmapGridResolution = ClipmapGridResolution;
			PassParameters->OutputGridResolution = OutputGridResolution;

			auto ComputeShader = View.ShaderMap->GetShader<FClearVoxelLightingClipmapCS>();
			const FIntVector GroupSize = FComputeShaderUtils::GetGroupCount(OutputGridResolution, FClearVoxelLightingClipmapCS::GetGroupSize());

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("ClearClipmap"),
				ComputeShader,
				PassParameters,
				GroupSize);
		}

		// Compact vis buffer
		{
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(CompactedVisBufferAllocator, PF_R32_UINT), 0);

			FCompactVisBufferCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCompactVisBufferCS::FParameters>();
			PassParameters->View = View.ViewUniformBuffer;
			PassParameters->RWCompactedVisBufferAllocator = GraphBuilder.CreateUAV(CompactedVisBufferAllocator, PF_R32_UINT);
			PassParameters->RWCompactedVisBuffer = GraphBuilder.CreateUAV(CompactedVisBuffer, PF_R32_UINT);
			PassParameters->ClipmapGridResolution = ClipmapGridResolution;
			PassParameters->ClipmapIndex = ClipmapIndex;
			PassParameters->VoxelVisBuffer = GraphBuilder.CreateSRV(VoxelVisBuffer, PF_R32_UINT);
			PassParameters->VoxelCoordToUVScale = (FVector3f)Clipmap.VoxelCoordToUVScale;
			PassParameters->VoxelCoordToUVBias = (FVector3f)Clipmap.VoxelCoordToUVBias;

			auto ComputeShader = View.ShaderMap->GetShader<FCompactVisBufferCS>();

			const FIntVector GroupSize = FComputeShaderUtils::GetGroupCount(ClipmapGridResolution, FCompactVisBufferCS::GetGroupSize());

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("CompactVisBuffer"),
				ComputeShader,
				PassParameters,
				GroupSize);
		}

		// Setup indirect arguments for the vis buffer shading
		{
			FSetupVisBufferShadingCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSetupVisBufferShadingCS::FParameters>();
			PassParameters->View = View.ViewUniformBuffer;
			PassParameters->RWCompactedVisBufferIndirectArguments = GraphBuilder.CreateUAV(CompactedVisBufferIndirectArguments, PF_R32_UINT);
			PassParameters->CompactedVisBufferAllocator = GraphBuilder.CreateSRV(CompactedVisBufferAllocator, PF_R32_UINT);

			auto ComputeShader = View.ShaderMap->GetShader<FSetupVisBufferShadingCS>();

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("SetupVisBufferShading"),
				ComputeShader,
				PassParameters,
				FIntVector(1, 1, 1));
		}

		// Vis buffer shading
		{
			FVisBufferShadingCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVisBufferShadingCS::FParameters>();
			PassParameters->RWVoxelLighting = VoxelLightingUAV;
			PassParameters->CompactedVisBufferIndirectArguments = CompactedVisBufferIndirectArguments;
			PassParameters->CompactedVisBufferAllocator = GraphBuilder.CreateSRV(CompactedVisBufferAllocator, PF_R32_UINT);
			PassParameters->CompactedVisBuffer = GraphBuilder.CreateSRV(CompactedVisBuffer, PF_R32_UINT);
			GetLumenCardTracingParameters(View, TracingInputs, ViewTracingInputs, PassParameters->TracingParameters, true);
			PassParameters->MeshSDFTracingParameters = MeshSDFTracingParameters;
			PassParameters->VoxelVisBuffer = GraphBuilder.CreateSRV(VoxelVisBuffer,PF_R32_UINT);
			PassParameters->ClipmapIndex = ClipmapIndex;
			PassParameters->ClipmapGridResolution = ClipmapGridResolution;
			PassParameters->VoxelCoordToUVScale = (FVector3f)Clipmap.VoxelCoordToUVScale;
			PassParameters->VoxelCoordToUVBias = (FVector3f)Clipmap.VoxelCoordToUVBias;
			PassParameters->GridMin = FVector3f(Clipmap.Center - Clipmap.Extent);
			PassParameters->GridVoxelSize = (FVector3f)Clipmap.VoxelSize;

			bool bDistantScene = false;
			if (GLumenSceneVoxelLightingDistantScene != 0
				&& LumenSceneData.DistantCardIndices.Num() > 0
				&& ClipmapIndex + 1 == GetNumLumenVoxelClipmaps(View.FinalPostProcessSettings.LumenSceneViewDistance))
			{
				bDistantScene = true;
			}

			FVisBufferShadingCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FVisBufferShadingCS::FDistantScene>(bDistantScene);
			PermutationVector.Set<FVisBufferShadingCS::FShadeMeshSDFDim>(NumDistanceFieldObjects > 0);
			PermutationVector.Set<FVisBufferShadingCS::FShadeHeightfieldDim>(Lumen::UseHeightfieldTracingForVoxelLighting(LumenSceneData));
			auto ComputeShader = View.ShaderMap->GetShader<FVisBufferShadingCS>(PermutationVector);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("VisBufferShading"),
				ComputeShader,
				PassParameters,
				CompactedVisBufferIndirectArguments,
				0);
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
		else if (ClipmapIndex == 4)
		{
			return FrameNumber % 32 == 15;
		}
		else
		{
			return FrameNumber % 32 == 31;
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

void AddUpdateBoundsForAxis(FIntVector MovementInTiles,
	const FBox& ClipmapBounds,
	float UpdateTileWorldSize,
	int32 ComponentIndex,
	TArray<FClipmapUpdateBounds, SceneRenderingAllocator>& UpdateBounds)
{
	FBox AxisUpdateBounds = ClipmapBounds;

	if (MovementInTiles[ComponentIndex] > 0)
	{
		AxisUpdateBounds.Min[ComponentIndex] = FMath::Max(ClipmapBounds.Max[ComponentIndex] - MovementInTiles[ComponentIndex] * UpdateTileWorldSize, ClipmapBounds.Min[ComponentIndex]);
	}
	else if (MovementInTiles[ComponentIndex] < 0)
	{
		AxisUpdateBounds.Max[ComponentIndex] = FMath::Min(ClipmapBounds.Min[ComponentIndex] - MovementInTiles[ComponentIndex] * UpdateTileWorldSize, ClipmapBounds.Max[ComponentIndex]);
	}

	if (FMath::Abs(MovementInTiles[ComponentIndex]) > 0)
	{
		const FVector CellCenterBias = FVector(-0.5f * UpdateTileWorldSize);
		UpdateBounds.Add(FClipmapUpdateBounds(AxisUpdateBounds.GetCenter(), AxisUpdateBounds.GetExtent() + CellCenterBias, false));
	}
}

void UpdateVoxelVisBuffer(
	FRDGBuilder& GraphBuilder, 
	FScene* Scene, 
	const FViewInfo& View,
	const FLumenSceneFrameTemporaries& FrameTemporaries,
	const FLumenCardTracingInputs& TracingInputs,
	FLumenViewCardTracingInputs& ViewTracingInputs,
	FRDGBufferRef VoxelVisBuffer,
	const TArray<int32, SceneRenderingAllocator>& ClipmapsToUpdate, 
	bool bForceFullUpdate)
{
	const FDistanceFieldSceneData& DistanceFieldSceneData = Scene->DistanceFieldSceneData;
	const uint32 NumDistanceFieldObjects = DistanceFieldSceneData.NumObjectsInBuffer;

	if ((NumVoxelizedObjects(DistanceFieldSceneData, *Scene->LumenSceneData) == 0) || View.ViewState == nullptr)
	{
		return;
	}

	const int32 ClampedNumClipmapLevels = GetNumLumenVoxelClipmaps(View.FinalPostProcessSettings.LumenSceneViewDistance);
	const FIntVector ClipmapResolution = GetClipmapResolution();

	// Copy scene modified primitives into clipmap state
	for (int32 ClipmapIndex = 0; ClipmapIndex < MaxVoxelClipmapLevels; ++ClipmapIndex)
	{
		TArray<FRenderBounds>& PrimitiveModifiedBounds = View.ViewState->Lumen.VoxelLightingClipmapState[ClipmapIndex].PrimitiveModifiedBounds;
		if (ClipmapIndex < ClampedNumClipmapLevels)
		{
			PrimitiveModifiedBounds.Append(Scene->LumenSceneData->PrimitiveModifiedBounds);
		}
		else
		{
			PrimitiveModifiedBounds.Empty();
		}
	}

	// Update clipmaps
	for (int32 ClipmapIndex : ClipmapsToUpdate)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "UpdateVoxelVisBuffer Clipmap:%d", ClipmapIndex);

		bool bForceFullClipmapUpdate = bForceFullUpdate || GLumenSceneVoxelLightingForceUpdateClipmapIndex == ClipmapIndex;
		FLumenViewState& LumenViewState = View.ViewState->Lumen;
		FLumenVoxelLightingClipmapState& Clipmap = LumenViewState.VoxelLightingClipmapState[ClipmapIndex];

		const FIntVector UpdateGridResolution = GetUpdateGridResolution();
		const FVector LumenSceneCameraOrigin = GetLumenSceneViewOrigin(View, ClipmapIndex);
		const FVector ClipmapExtent = GetLumenVoxelClipmapExtent(ClipmapIndex);

		const FVector UpdateTileWorldSize = (2.0f * ClipmapExtent) / FVector(UpdateGridResolution);
		FIntVector UpdateTileCenter;
		UpdateTileCenter.X = FMath::RoundToInt(LumenSceneCameraOrigin.X / UpdateTileWorldSize.X);
		UpdateTileCenter.Y = FMath::RoundToInt(LumenSceneCameraOrigin.Y / UpdateTileWorldSize.Y);
		UpdateTileCenter.Z = FMath::RoundToInt(LumenSceneCameraOrigin.Z / UpdateTileWorldSize.Z);

		Clipmap.Center = FVector(UpdateTileCenter) * UpdateTileWorldSize;
		Clipmap.Extent = ClipmapExtent;
		Clipmap.VoxelSize = 2.0f * Clipmap.Extent / FVector(ClipmapResolution);
		Clipmap.VoxelRadius = Clipmap.VoxelSize.Size();

		const float RadiusThresholdScale = 1.0f / FMath::Clamp(View.FinalPostProcessSettings.LumenSceneDetail, .01f, 100.0f);
		const float NewMeshSDFRadiusThreshold = Clipmap.VoxelRadius * GLumenSceneVoxelLightingMeshSDFRadiusThresholdFactor * RadiusThresholdScale;
		if (fabs(Clipmap.MeshSDFRadiusThreshold - NewMeshSDFRadiusThreshold) > 1.0f)
		{
			Clipmap.MeshSDFRadiusThreshold = NewMeshSDFRadiusThreshold;
			bForceFullClipmapUpdate = true;
		}

		ViewTracingInputs.ClipmapWorldToUVScale[ClipmapIndex] = FVector(1.0f) / (2.0f * Clipmap.Extent);
		ViewTracingInputs.ClipmapWorldToUVBias[ClipmapIndex] = -(Clipmap.Center - Clipmap.Extent) * ViewTracingInputs.ClipmapWorldToUVScale[ClipmapIndex];
		ViewTracingInputs.ClipmapVoxelSizeAndRadius[ClipmapIndex] = FVector4f((FVector3f)Clipmap.VoxelSize, Clipmap.VoxelRadius);
		ViewTracingInputs.ClipmapWorldCenter[ClipmapIndex] = Clipmap.Center;
		ViewTracingInputs.ClipmapWorldExtent[ClipmapIndex] = Clipmap.Extent;
		ViewTracingInputs.ClipmapWorldSamplingExtent[ClipmapIndex] = Clipmap.Extent - 0.5f * Clipmap.VoxelSize;
		
		TArray<FRenderBounds>& PrimitiveModifiedBounds = Clipmap.PrimitiveModifiedBounds;
		PrimitiveModifiedBounds.Append(Scene->LumenSceneData->PrimitiveModifiedBounds);

		const FBox ClipmapBounds(Clipmap.Center - Clipmap.Extent, Clipmap.Center + Clipmap.Extent);
		TArray<FClipmapUpdateBounds, SceneRenderingAllocator> UpdateBounds;

		if (bForceFullClipmapUpdate)
		{
			Clipmap.FullUpdateOriginInTiles = UpdateTileCenter;
			UpdateBounds.Add(FClipmapUpdateBounds(ClipmapBounds.GetCenter(), ClipmapBounds.GetExtent(), false));
		}
		else
		{
			TArray<FBox, SceneRenderingAllocator> CulledPrimitiveModifiedBounds;
			CulledPrimitiveModifiedBounds.Empty(PrimitiveModifiedBounds.Num() / 2);

			for (int32 BoundsIndex = 0; BoundsIndex < PrimitiveModifiedBounds.Num(); BoundsIndex++)
			{
				const FRenderBounds PrimBounds = PrimitiveModifiedBounds[BoundsIndex];
				const FVector PrimWorldCenter = (FVector)PrimBounds.GetCenter();
				const FVector PrimWorldExtent = (FVector)PrimBounds.GetExtent();
				const FBox ModifiedBounds(PrimWorldCenter - PrimWorldExtent, PrimWorldCenter + PrimWorldExtent);

				if (ModifiedBounds.Intersect(ClipmapBounds))
				{
					CulledPrimitiveModifiedBounds.Add(ModifiedBounds);

					UpdateBounds.Add(FClipmapUpdateBounds(ModifiedBounds.GetCenter(), ModifiedBounds.GetExtent(), true));
				}
			}

			// Add an update region for each potential axis of camera movement
			FIntVector MovementInTiles = UpdateTileCenter - Clipmap.LastPartialUpdateOriginInTiles;
			if (GLumenSceneVoxelLightingForceMovementUpdate != 0)
			{
				MovementInTiles = FIntVector(GLumenSceneVoxelLightingForceMovementUpdate);
			}
			AddUpdateBoundsForAxis(MovementInTiles, ClipmapBounds, UpdateTileWorldSize[0], 0, UpdateBounds);
			AddUpdateBoundsForAxis(MovementInTiles, ClipmapBounds, UpdateTileWorldSize[1], 1, UpdateBounds);
			AddUpdateBoundsForAxis(MovementInTiles, ClipmapBounds, UpdateTileWorldSize[2], 2, UpdateBounds);
		}

		PrimitiveModifiedBounds.Empty(DistanceField::MinPrimitiveModifiedBoundsAllocation);
		Clipmap.LastPartialUpdateOriginInTiles = UpdateTileCenter;
		Clipmap.ScrollOffsetInTiles = Clipmap.LastPartialUpdateOriginInTiles - Clipmap.FullUpdateOriginInTiles;
		Clipmap.VoxelCoordToUVScale = FVector(1.0f) / FVector(ClipmapResolution);
		Clipmap.VoxelCoordToUVBias = (FVector(Clipmap.ScrollOffsetInTiles) + FVector(0.5f)) / FVector(UpdateGridResolution);
		
		if (UpdateBounds.Num() > 0)
		{
			// Upload update bounds data
			FRDGBufferRef UpdateBoundsBuffer = nullptr;
			uint32 NumUpdateBounds = 0;
			{
				const uint32 BufferStrideInFloat4 = 2;
				FRDGUploadData<FVector4f> UpdateBoundsData(GraphBuilder, BufferStrideInFloat4 * UpdateBounds.Num());

				for (int32 UpdateBoundsIndex = 0; UpdateBoundsIndex < UpdateBounds.Num(); ++UpdateBoundsIndex)
				{
					const FClipmapUpdateBounds& Bounds = UpdateBounds[UpdateBoundsIndex];

					UpdateBoundsData[NumUpdateBounds * BufferStrideInFloat4 + 0] = FVector4f((FVector3f)Bounds.Center, 0.0f);
					UpdateBoundsData[NumUpdateBounds * BufferStrideInFloat4 + 1] = FVector4f((FVector3f)Bounds.Extent, 0.0f);
					++NumUpdateBounds;
				}

				check(UpdateBoundsData.Num() % BufferStrideInFloat4 == 0);

				UpdateBoundsBuffer =
					CreateUploadBuffer(GraphBuilder, TEXT("Lumen.UpdateBoundsBuffer"),
						sizeof(FVector4f), FMath::RoundUpToPowerOfTwo(FMath::Max(UpdateBoundsData.Num(), 2)),
						UpdateBoundsData);
			}

			FRDGBufferRef ClearVisBufferIndirectArgBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(13), TEXT("Lumen.UpdateIndirectArgBuffer"));
			FRDGBufferRef TraceSetupIndirectArgBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(13), TEXT("Lumen.TraceSetupIndirectArgBuffer"));
			FRDGBufferRef TraceIndirectArgBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(13), TEXT("Lumen.TraceIndirectArgBuffer"));
			{
				FClearIndirectAgrBuffersCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FClearIndirectAgrBuffersCS::FParameters>();
				PassParameters->RWClearVisBufferIndirectArgBuffer = GraphBuilder.CreateUAV(ClearVisBufferIndirectArgBuffer, PF_R32_UINT);
				PassParameters->RWTraceSetupIndirectArgBuffer = GraphBuilder.CreateUAV(TraceSetupIndirectArgBuffer, PF_R32_UINT);
				PassParameters->RWTraceIndirectArgBuffer = GraphBuilder.CreateUAV(TraceIndirectArgBuffer, PF_R32_UINT);

				auto ComputeShader = View.ShaderMap->GetShader<FClearIndirectAgrBuffersCS>();

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("ClearIndirectArgBuffer"),
					ComputeShader,
					PassParameters,
					FIntVector(1, 1, 1));
			}

			const FVector UpdateTileWorldExtent = 0.5f * Clipmap.VoxelSize * GVisBufferTileSize;
			const FVector UpdateGridCoordToWorldCenterScale = (2.0f * Clipmap.Extent) / FVector(UpdateGridResolution);
			const FVector UpdateGridCoordToWorldCenterBias = Clipmap.Center - Clipmap.Extent + 0.5f * UpdateGridCoordToWorldCenterScale;

			const uint32 UpdateGridSize = UpdateGridResolution.X * UpdateGridResolution.Y * UpdateGridResolution.Z;
			FRDGBufferRef UpdateTileBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), UpdateGridSize), TEXT("Lumen.UpdateTileBuffer"));

			FRDGTextureDesc UpdateTileMaskDesc(FRDGTextureDesc::Create3D(
				UpdateGridResolution,
				PF_R8_UINT,
				FClearValueBinding::Black,
				TexCreate_ShaderResource | TexCreate_UAV | TexCreate_3DTiling));
			FRDGTextureRef UpdateTileMaskTexture = GraphBuilder.CreateTexture(UpdateTileMaskDesc, TEXT("Lumen.UpdateTileMask"));
			
			// Prepare tiles which need to be updated
			{
				FBuildUpdateGridTilesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FBuildUpdateGridTilesCS::FParameters>();
				PassParameters->View = View.ViewUniformBuffer;
				PassParameters->RWGridTileBuffer = GraphBuilder.CreateUAV(UpdateTileBuffer, PF_R32_UINT);
				PassParameters->RWGridTileMaskTexture = GraphBuilder.CreateUAV(UpdateTileMaskTexture);
				PassParameters->RWClearVisBufferIndirectArgBuffer = GraphBuilder.CreateUAV(ClearVisBufferIndirectArgBuffer, PF_R32_UINT);
				PassParameters->UpdateBoundsBuffer = GraphBuilder.CreateSRV(UpdateBoundsBuffer, PF_A32B32G32R32F);
				PassParameters->NumUpdateBounds = NumUpdateBounds;
				PassParameters->GridResolution = UpdateGridResolution;
				PassParameters->GridCoordToWorldCenterScale = (FVector3f)UpdateGridCoordToWorldCenterScale;
				PassParameters->GridCoordToWorldCenterBias = (FVector3f)UpdateGridCoordToWorldCenterBias;
				PassParameters->TileWorldExtent = (FVector3f)UpdateTileWorldExtent;

				auto ComputeShader = View.ShaderMap->GetShader<FBuildUpdateGridTilesCS>();

				const FIntVector GroupSize = UpdateGridResolution;

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("FBuildUpdateGridTiles NumUpdateBounds:%d", NumUpdateBounds),
					ComputeShader,
					PassParameters,
					GroupSize);
			}

			// Clear updated visibility buffer tiles
			{
				FClearVisBuffer::FParameters* PassParameters = GraphBuilder.AllocParameters<FClearVisBuffer::FParameters>();
				PassParameters->RWVoxelVisBuffer = GraphBuilder.CreateUAV(VoxelVisBuffer, PF_R32_UINT);
				PassParameters->UpdateTileBuffer = GraphBuilder.CreateSRV(UpdateTileBuffer, PF_R32_UINT);
				PassParameters->ClearVisBufferIndirectArgBuffer = ClearVisBufferIndirectArgBuffer;
				PassParameters->ClipmapIndex = ClipmapIndex;
				PassParameters->ClipmapGridResolution = ClipmapResolution;
				PassParameters->VoxelCoordToUVScale = (FVector3f)Clipmap.VoxelCoordToUVScale;
				PassParameters->VoxelCoordToUVBias = (FVector3f)Clipmap.VoxelCoordToUVBias;

				auto ComputeShader = View.ShaderMap->GetShader<FClearVisBuffer>();

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("ClearVisBuffer"),
					ComputeShader,
					PassParameters,
					ClearVisBufferIndirectArgBuffer,
					0);
			}

			const int32 MaxSDFMeshObjects = FMath::RoundUpToPowerOfTwo(DistanceFieldSceneData.NumObjectsInBuffer);
			FRDGBufferRef ObjectIndexBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), MaxSDFMeshObjects), TEXT("Lumen.ObjectIndices"));

			const uint32 AverageObjectsPerVisBufferTile = FMath::Clamp(GLumenSceneVoxelLightingAverageObjectsPerVisBufferTile, 1, 8192);
			FRDGBufferRef VoxelTraceData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(2 * sizeof(uint32), UpdateGridSize * AverageObjectsPerVisBufferTile), TEXT("Lumen.VoxelTraceData"));

			if (NumDistanceFieldObjects > 0)
			{
				// Cull to clipmap
				{
					FCullToVoxelClipmapCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCullToVoxelClipmapCS::FParameters>();
					PassParameters->RWObjectIndexBuffer = GraphBuilder.CreateUAV(ObjectIndexBuffer, PF_R32_UINT);
					PassParameters->RWTraceSetupIndirectArgBuffer = GraphBuilder.CreateUAV(TraceSetupIndirectArgBuffer, PF_R32_UINT);
					PassParameters->DistanceFieldObjectBuffers = DistanceField::SetupObjectBufferParameters(GraphBuilder, DistanceFieldSceneData);
					PassParameters->VoxelClipmapWorldCenter = (FVector3f)Clipmap.Center;
					PassParameters->VoxelClipmapWorldExtent = (FVector3f)Clipmap.Extent;
					PassParameters->MeshRadiusThreshold = Clipmap.MeshSDFRadiusThreshold;

					FCullToVoxelClipmapCS::FPermutationDomain PermutationVector;
					PermutationVector.Set<FCullToVoxelClipmapCS::FMeshTypeDim>(EMeshType::SDF);
					auto ComputeShader = View.ShaderMap->GetShader<FCullToVoxelClipmapCS>(PermutationVector);
					const FIntVector GroupSize = FComputeShaderUtils::GetGroupCount(DistanceFieldSceneData.NumObjectsInBuffer, FCullToVoxelClipmapCS::GetGroupSize());

					FComputeShaderUtils::AddPass(
						GraphBuilder,
						RDG_EVENT_NAME("CullToClipmap<SDF>"),
						ComputeShader,
						PassParameters,
						GroupSize);
				}

				// Setup voxel traces
				{
					FSetupVoxelTracesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSetupVoxelTracesCS::FParameters>();
					PassParameters->RWTraceIndirectArgBuffer = GraphBuilder.CreateUAV(TraceIndirectArgBuffer, PF_R32_UINT);
					PassParameters->RWVoxelTraceData = GraphBuilder.CreateUAV(VoxelTraceData, PF_R32_UINT);
					PassParameters->UpdateTileMaskTexture = UpdateTileMaskTexture;
					PassParameters->ObjectIndexBuffer = GraphBuilder.CreateSRV(ObjectIndexBuffer, PF_R32_UINT);
					PassParameters->TraceSetupIndirectArgBuffer = TraceSetupIndirectArgBuffer;
					PassParameters->DistanceFieldObjectBuffers = DistanceField::SetupObjectBufferParameters(GraphBuilder, DistanceFieldSceneData);
					PassParameters->UpdateGridResolution = UpdateGridResolution;
					PassParameters->ClipmapToUpdateGridScale = FVector3f(1.0f) / (2.0f * (FVector3f)UpdateTileWorldExtent);
					PassParameters->ClipmapToUpdateGridBias = FVector3f(-(Clipmap.Center - Clipmap.Extent) / (2.0f * UpdateTileWorldExtent) + 0.5f);
					PassParameters->ConservativeRasterizationExtent = (FVector3f)UpdateTileWorldExtent;

					FSetupVoxelTracesCS::FPermutationDomain PermutationVector;
					PermutationVector.Set<FSetupVoxelTracesCS::FMeshTypeDim>(EMeshType::SDF);
					auto ComputeShader = View.ShaderMap->GetShader<FSetupVoxelTracesCS>(PermutationVector);

					FComputeShaderUtils::AddPass(
						GraphBuilder,
						RDG_EVENT_NAME("SetupVoxelTraces"),
						ComputeShader,
						PassParameters,
						TraceSetupIndirectArgBuffer,
						0);
				}

				FLumenMeshSDFTracingParameters MeshSDFTracingParameters;
				SetupLumenMeshSDFTracingParameters(GraphBuilder, Scene, View, MeshSDFTracingParameters);

				// Voxel tracing
				{
					FVoxelTraceCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVoxelTraceCS::FParameters>();
                    PassParameters->RWVoxelVisBuffer = GraphBuilder.CreateUAV(VoxelVisBuffer, PF_R32_UINT);
					GetLumenCardTracingParameters(View, TracingInputs, ViewTracingInputs, PassParameters->TracingParameters, true);
					PassParameters->TraceIndirectArgBuffer = TraceIndirectArgBuffer;
					PassParameters->VoxelTraceData = GraphBuilder.CreateSRV(VoxelTraceData, PF_R32_UINT);
					PassParameters->MeshSDFTracingParameters = MeshSDFTracingParameters;
					PassParameters->ClipmapIndex = ClipmapIndex;
					PassParameters->ClipmapGridResolution = ClipmapResolution;
					PassParameters->GridMin = FVector3f(Clipmap.Center - Clipmap.Extent);
					PassParameters->GridVoxelSize = (FVector3f)Clipmap.VoxelSize;
					PassParameters->CullGridResolution = UpdateGridResolution;
					PassParameters->VoxelCoordToUVScale = (FVector3f)Clipmap.VoxelCoordToUVScale;
					PassParameters->VoxelCoordToUVBias = (FVector3f)Clipmap.VoxelCoordToUVBias;

					auto ComputeShader = View.ShaderMap->GetShader<FVoxelTraceCS>();

					FComputeShaderUtils::AddPass(
						GraphBuilder,
						RDG_EVENT_NAME("VoxelTraceCS"),
						ComputeShader,
						PassParameters,
						TraceIndirectArgBuffer,
						0);
				}
			}

			// Height-field voxelization
			const FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;
			if (Lumen::UseHeightfieldTracingForVoxelLighting(LumenSceneData))
			{
				// Clear indirect args
				FRDGBufferRef DummyClearVisBufferIndirectArgBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("Lumen.UpdateIndirectArgBuffer"));
				{
					FClearIndirectAgrBuffersCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FClearIndirectAgrBuffersCS::FParameters>();
					PassParameters->RWClearVisBufferIndirectArgBuffer = GraphBuilder.CreateUAV(DummyClearVisBufferIndirectArgBuffer, PF_R32_UINT);
					PassParameters->RWTraceSetupIndirectArgBuffer = GraphBuilder.CreateUAV(TraceSetupIndirectArgBuffer, PF_R32_UINT);
					PassParameters->RWTraceIndirectArgBuffer = GraphBuilder.CreateUAV(TraceIndirectArgBuffer, PF_R32_UINT);

					auto ComputeShader = View.ShaderMap->GetShader<FClearIndirectAgrBuffersCS>();

					FComputeShaderUtils::AddPass(
						GraphBuilder,
						RDG_EVENT_NAME("ClearIndirectArgBuffer"),
						ComputeShader,
						PassParameters,
						FIntVector(1, 1, 1));
				}
				const uint32 NumHeightfieldObjects = LumenSceneData.Heightfields.Num();
				uint32 MaxNumHeightfieldObjects = FMath::RoundUpToPowerOfTwo(NumHeightfieldObjects);

				FRDGBufferRef HeightfieldObjectIndexBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), MaxNumHeightfieldObjects), TEXT("Lumen.HeightfieldObjectIndices"));

				AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(HeightfieldObjectIndexBuffer, PF_R32_UINT), 0);

				// Cull to clipmap
				{
					FCullToVoxelClipmapCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCullToVoxelClipmapCS::FParameters>();
					PassParameters->RWObjectIndexBuffer = GraphBuilder.CreateUAV(HeightfieldObjectIndexBuffer, PF_R32_UINT);
					PassParameters->RWTraceSetupIndirectArgBuffer = GraphBuilder.CreateUAV(TraceSetupIndirectArgBuffer, PF_R32_UINT);
					PassParameters->View = View.ViewUniformBuffer;
					PassParameters->LumenCardScene = TracingInputs.LumenCardSceneUniformBuffer;
					PassParameters->VoxelClipmapWorldCenter = (FVector3f)Clipmap.Center;
					PassParameters->VoxelClipmapWorldExtent = (FVector3f)Clipmap.Extent;
					PassParameters->MeshRadiusThreshold = Clipmap.MeshSDFRadiusThreshold;

					FCullToVoxelClipmapCS::FPermutationDomain PermutationVector;
					PermutationVector.Set<FCullToVoxelClipmapCS::FMeshTypeDim>(EMeshType::Heightfield);
					auto ComputeShader = View.ShaderMap->GetShader<FCullToVoxelClipmapCS>(PermutationVector);
					const FIntVector GroupSize = FComputeShaderUtils::GetGroupCount(NumHeightfieldObjects, FCullToVoxelClipmapCS::GetGroupSize());

					FComputeShaderUtils::AddPass(
						GraphBuilder,
						RDG_EVENT_NAME("CullToClipmap<Heightfield>"),
						ComputeShader,
						PassParameters,
						GroupSize);
				}

				// Tag voxel/Heightfield pairs
				{
					FSetupVoxelTracesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSetupVoxelTracesCS::FParameters>();
					PassParameters->RWTraceIndirectArgBuffer = GraphBuilder.CreateUAV(TraceIndirectArgBuffer, PF_R32_UINT);
					PassParameters->RWVoxelTraceData = GraphBuilder.CreateUAV(VoxelTraceData, PF_R32_UINT);
					PassParameters->UpdateTileMaskTexture = UpdateTileMaskTexture;
					PassParameters->ObjectIndexBuffer = GraphBuilder.CreateSRV(HeightfieldObjectIndexBuffer, PF_R32_UINT);
					PassParameters->TraceSetupIndirectArgBuffer = TraceSetupIndirectArgBuffer;
					PassParameters->View = View.ViewUniformBuffer;
					PassParameters->LumenCardScene = TracingInputs.LumenCardSceneUniformBuffer;
					PassParameters->UpdateGridResolution = UpdateGridResolution;
					PassParameters->ClipmapToUpdateGridScale = FVector3f(1.0f) / (2.0f * (FVector3f)UpdateTileWorldExtent);
					PassParameters->ClipmapToUpdateGridBias = FVector3f(-(Clipmap.Center - Clipmap.Extent) / (2.0f * UpdateTileWorldExtent) + 0.5f);
					PassParameters->ConservativeRasterizationExtent = (FVector3f)UpdateTileWorldExtent;

					FSetupVoxelTracesCS::FPermutationDomain PermutationVector;
					PermutationVector.Set<FSetupVoxelTracesCS::FMeshTypeDim>(EMeshType::Heightfield);
					auto ComputeShader = View.ShaderMap->GetShader<FSetupVoxelTracesCS>(PermutationVector);

					FComputeShaderUtils::AddPass(
						GraphBuilder,
						RDG_EVENT_NAME("SetupVoxelTraces<Heightfield>"),
						ComputeShader,
						PassParameters,
						TraceSetupIndirectArgBuffer,
						0);
				}

				// Trace Heightfield
				{

					FHeightfieldVoxelTraceCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FHeightfieldVoxelTraceCS::FParameters>();
                    PassParameters->RWVoxelVisBuffer = GraphBuilder.CreateUAV(VoxelVisBuffer, PF_R32_UINT);
					GetLumenCardTracingParameters(View, TracingInputs, ViewTracingInputs, PassParameters->TracingParameters, true);
					PassParameters->TraceIndirectArgBuffer = TraceIndirectArgBuffer;
					PassParameters->VoxelTraceData = GraphBuilder.CreateSRV(VoxelTraceData, PF_R32_UINT);
					PassParameters->ClipmapIndex = ClipmapIndex;
					PassParameters->ClipmapGridResolution = ClipmapResolution;
					PassParameters->GridMin = FVector3f(Clipmap.Center - Clipmap.Extent);
					PassParameters->GridVoxelSize = (FVector3f)Clipmap.VoxelSize;
					PassParameters->CullGridResolution = UpdateGridResolution;
					PassParameters->VoxelCoordToUVScale = (FVector3f)Clipmap.VoxelCoordToUVScale;
					PassParameters->VoxelCoordToUVBias = (FVector3f)Clipmap.VoxelCoordToUVBias;
					PassParameters->HeightfieldMaxTracingSteps = Lumen::GetHeightfieldMaxTracingSteps();

					auto ComputeShader = View.ShaderMap->GetShader<FHeightfieldVoxelTraceCS>();

					FComputeShaderUtils::AddPass(
						GraphBuilder,
						RDG_EVENT_NAME("HeightfieldVoxelTraceCS"),
						ComputeShader,
						PassParameters,
						TraceIndirectArgBuffer,
						0);
				}
			}
		}
	}
}

void FDeferredShadingSceneRenderer::ComputeLumenSceneVoxelLighting(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FLumenSceneFrameTemporaries& FrameTemporaries,
	const FLumenCardTracingInputs& TracingInputs,
	FLumenViewCardTracingInputs& ViewTracingInputs)
{
	LLM_SCOPE_BYTAG(Lumen);

	if (!Lumen::UseVoxelLighting(ViewFamily))
	{
		// No need for voxel lighting, skip update and release resources
		View.ViewState->Lumen.VoxelVisBuffer = nullptr;
		View.ViewState->Lumen.VoxelLighting = nullptr;
		return;
	}

	const int32 ClampedNumClipmapLevels = GetNumLumenVoxelClipmaps(View.FinalPostProcessSettings.LumenSceneViewDistance);
	const FIntVector ClipmapResolution = GetClipmapResolution();
	bool bForceFullUpdate = GLumenSceneVoxelLightingForceFullUpdate != 0 || GLumenSceneVoxelLightingReset != 0;
	GLumenSceneVoxelLightingReset = 0;

	FRDGTextureRef VoxelLighting = ViewTracingInputs.VoxelLighting;
	{
		FRDGTextureDesc LightingDesc(FRDGTextureDesc::Create3D(
			FIntVector(
				ClipmapResolution.X,
				ClipmapResolution.Y * ClampedNumClipmapLevels,
				ClipmapResolution.Z * GNumVoxelDirections),
			PF_FloatRGBA,
			FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_UAV | TexCreate_3DTiling));

		if (!VoxelLighting || VoxelLighting->Desc != LightingDesc)
		{
			bForceFullUpdate = true;
			VoxelLighting = GraphBuilder.CreateTexture(LightingDesc, TEXT("Lumen.VoxelLighting"));
		}
	}

	FRDGBufferRef VoxelVisBuffer = View.ViewState->Lumen.VoxelVisBuffer ? GraphBuilder.RegisterExternalBuffer(View.ViewState->Lumen.VoxelVisBuffer) : nullptr;
	{
		FIntVector VoxelVisBufferDimension = AlignVisBufferSizeToTileSize(
			FIntVector(ClipmapResolution.X,
			ClipmapResolution.Y * ClampedNumClipmapLevels,
			ClipmapResolution.Z * GNumVoxelDirections));

		FRDGBufferDesc VoxelVisBufferDesc = FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32),
			VoxelVisBufferDimension.X *
			VoxelVisBufferDimension.Y *
			VoxelVisBufferDimension.Z);
        
		if (!VoxelVisBuffer
			|| VoxelVisBuffer->Desc.BytesPerElement != VoxelVisBufferDesc.BytesPerElement
			|| VoxelVisBuffer->Desc.NumElements != VoxelVisBufferDesc.NumElements)
		{
			bForceFullUpdate = true;
			VoxelVisBuffer = GraphBuilder.CreateBuffer(VoxelVisBufferDesc, TEXT("Lumen.VoxelVisBuffer"));

			uint32 VisBufferClearValue = 0xFFFFFFFF;
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(VoxelVisBuffer, PF_R32_UINT), VisBufferClearValue);
		}
	}

	// Vis buffer data is valid only for a particular scene and need to be recreated if scene changes
	if (View.ViewState->Lumen.VoxelVisBufferCachedScene != Scene)
	{
		bForceFullUpdate = true;
		View.ViewState->Lumen.VoxelVisBufferCachedScene = Scene;
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
		ViewTracingInputs.VoxelLighting = VoxelLighting;
		ViewTracingInputs.VoxelGridResolution = GetClipmapResolution();
		ViewTracingInputs.NumClipmapLevels = ClampedNumClipmapLevels;

		UpdateVoxelVisBuffer(GraphBuilder, Scene, View, FrameTemporaries, TracingInputs, ViewTracingInputs, VoxelVisBuffer, ClipmapsToUpdate, bForceFullUpdate);
		VoxelizeVisBuffer(View, Scene, TracingInputs, ViewTracingInputs, VoxelLighting, VoxelVisBuffer, ClipmapsToUpdate, GraphBuilder);

		View.ViewState->Lumen.VoxelLighting = GraphBuilder.ConvertToExternalTexture(VoxelLighting);
		View.ViewState->Lumen.VoxelGridResolution = ViewTracingInputs.VoxelGridResolution;
		View.ViewState->Lumen.NumClipmapLevels = ViewTracingInputs.NumClipmapLevels;
	}

	View.ViewState->Lumen.VoxelVisBuffer = GraphBuilder.ConvertToExternalBuffer(VoxelVisBuffer);
}

void Lumen::ExpandDistanceFieldUpdateTrackingBounds(const FSceneViewState* ViewState, DistanceField::FUpdateTrackingBounds& UpdateTrackingBounds)
{
	// Lumen is interested in any updates inside it's voxel lighting clipmaps

	for (int32 ClipmapIndex = 0; ClipmapIndex < ViewState->Lumen.NumClipmapLevels; ++ClipmapIndex)
	{
		const FLumenVoxelLightingClipmapState& Clipmap = ViewState->Lumen.VoxelLightingClipmapState[ClipmapIndex];
		const FBox TrackingBounds(Clipmap.Center - Clipmap.Extent, Clipmap.Center + Clipmap.Extent);
		UpdateTrackingBounds.LumenBounds += TrackingBounds;
	}
}