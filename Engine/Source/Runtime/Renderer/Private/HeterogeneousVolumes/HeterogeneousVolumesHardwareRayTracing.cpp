// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeterogeneousVolumes.h"

#include "PixelShaderUtils.h"
#include "RayTracingDefinitions.h"
#include "RayTracingInstance.h"
#include "RayTracingInstanceBufferUtil.h"
#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneManagement.h"

#if RHI_RAYTRACING
FIntVector GetVoxelCoord(uint32 VoxelIndex, FIntVector VolumeResolution)
{
	FIntVector VoxelCoord;

	uint32 SliceSize = VolumeResolution.X * VolumeResolution.Y;
	uint32 SliceIndex = VoxelIndex / SliceSize;
	uint32 SliceCoord = VoxelIndex - SliceIndex * SliceSize;

	VoxelCoord.X = SliceCoord % VolumeResolution.X;
	VoxelCoord.Y = SliceCoord / VolumeResolution.X;
	VoxelCoord.Z = SliceIndex;

	return VoxelCoord;
}

FBox GetVoxelBounds(uint32 VoxelIndex, FIntVector VolumeResolution, FVector LocalBoundsOrigin, FVector LocalBoundsExtent)
{
	FBox VoxelBounds;

	FIntVector VoxelCoord = GetVoxelCoord(VoxelIndex, VolumeResolution);
	FVector VoxelSize = (LocalBoundsExtent * 2.0) / FVector(VolumeResolution);

	VoxelBounds.Min = LocalBoundsOrigin - LocalBoundsExtent + FVector(VoxelCoord) * VoxelSize;
	VoxelBounds.Max = VoxelBounds.Min + VoxelSize;
	return VoxelBounds;
}

class FCreateSparseVoxelBLAS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCreateSparseVoxelBLAS);
	SHADER_USE_PARAMETER_STRUCT(FCreateSparseVoxelBLAS, FGlobalShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportHeterogeneousVolumes(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(
		const FGlobalShaderPermutationParameters& Parameters,
		FShaderCompilerEnvironment& OutEnvironment
	)
	{
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// Input
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSparseVoxelUniformBufferParameters, SparseVoxelUniformBuffer)

		// Output
		// Using RWStructuredBuffer<float> instead of RWStructuredBuffer<float3> to overcome Vulkan alignment error:
		// error: cannot instantiate RWStructuredBuffer with given packed alignment; 'VK_EXT_scalar_block_layout' not supported
		// SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<FVector>, RWPositionBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float>, RWPositionBuffer)

		// Indirect args
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FCreateSparseVoxelBLAS, "/Engine/Private/HeterogeneousVolumes/HeterogeneousVolumesSparseVoxelPipeline.usf", "CreateSparseVoxelBLAS", SF_Compute);

void CreateSparseVoxelBLAS(
	FRDGBuilder& GraphBuilder,
	// Scene data
	const FViewInfo& View,
	// Sparse voxel data
	TRDGUniformBufferRef<FSparseVoxelUniformBufferParameters> SparseVoxelUniformBuffer,
	FRDGBufferRef NumVoxelsBuffer,
	// Output
	FRDGBufferRef PositionBuffer
)
{
	FCreateSparseVoxelBLAS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCreateSparseVoxelBLAS::FParameters>();
	{
		PassParameters->SparseVoxelUniformBuffer = SparseVoxelUniformBuffer;
		PassParameters->RWPositionBuffer = GraphBuilder.CreateUAV(PositionBuffer);
		PassParameters->IndirectArgs = NumVoxelsBuffer;
	}

	TShaderRef<FCreateSparseVoxelBLAS> ComputeShader = View.ShaderMap->GetShader<FCreateSparseVoxelBLAS>();

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("CreateSparseVoxelBLAS"),
		ERDGPassFlags::Compute,
		ComputeShader,
		PassParameters,
		PassParameters->IndirectArgs,
		0);
}

void GenerateRayTracingGeometryInstance(
	FRDGBuilder& GraphBuilder,
	// Scene data
	const FScene* Scene,
	const FViewInfo& View,
	// Object data
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	// Volume data
	// Sparse voxel data
	FRDGBufferRef NumVoxelsBuffer,
	TRDGUniformBufferRef<FSparseVoxelUniformBufferParameters> SparseVoxelUniformBuffer,
	// Output
	TArray<FRayTracingGeometryRHIRef>& RayTracingGeometries,
	TArray<FMatrix>& RayTracingTransforms
)
{
	FIntVector MipVolumeResolution = HeterogeneousVolumes::GetMipVolumeResolution(SparseVoxelUniformBuffer->GetParameters()->VolumeResolution, SparseVoxelUniformBuffer->GetParameters()->MipLevel);
	uint32 MipVoxelCount = HeterogeneousVolumes::GetVoxelCount(MipVolumeResolution);

	TRefCountPtr<FRDGPooledBuffer> PooledVertexBuffer;
	{
#if 0
		TResourceArray<FVector3f> PositionData;

		PositionData.SetNumUninitialized(MipVoxelCount * 2);
		for (uint32 MipVoxelIndex = 0; MipVoxelIndex < MipVoxelCount; ++MipVoxelIndex)
		{
			FBox VoxelBounds = GetVoxelBounds(MipVoxelIndex, MipVolumeResolution, FVector(SparseVoxelUniformBuffer->GetParameters()->LocalBoundsOrigin), FVector(SparseVoxelUniformBuffer->GetParameters()->LocalBoundsExtent));

			uint32 BoundsIndex = MipVoxelIndex * 2;
			PositionData[BoundsIndex] = FVector3f(VoxelBounds.Min);
			PositionData[BoundsIndex + 1] = FVector3f(VoxelBounds.Max);
		}

		FRHIResourceCreateInfo CreateInfo(TEXT("HeterogeneousVolumesVB"));
		CreateInfo.ResourceArray = &PositionData;
#endif
		FRDGBufferRef VertexBuffer = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector3f), MipVoxelCount * 2),
		TEXT("CreateSparseVoxelBLAS.VertexBuffer"));

		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(VertexBuffer), 0.0);
		PooledVertexBuffer = GraphBuilder.ConvertToExternalBuffer(VertexBuffer);
	}

	// Morphs the dense-voxel topology into the sparse-voxel topology.
	CreateSparseVoxelBLAS(GraphBuilder, View, SparseVoxelUniformBuffer, NumVoxelsBuffer, GraphBuilder.RegisterExternalBuffer(PooledVertexBuffer));

	FRayTracingGeometryInitializer GeometryInitializer;
	// TODO: REMOVE STRING ALLOCATION
	GeometryInitializer.DebugName = *PrimitiveSceneProxy->GetResourceName().ToString();// +TEXT(" (HeterogeneousVolume)");
	//GeometryInitializer.IndexBuffer = EmptyIndexBuffer;
	GeometryInitializer.GeometryType = RTGT_Procedural;
	GeometryInitializer.bFastBuild = false;

	FRayTracingGeometrySegment Segment;
	Segment.NumPrimitives = MipVoxelCount;
	Segment.MaxVertices = MipVoxelCount * 2;
	Segment.VertexBufferStride = 2u * sizeof(FVector3f);
	Segment.VertexBuffer = PooledVertexBuffer->GetRHI();

	GeometryInitializer.Segments.Add(Segment);
	GeometryInitializer.TotalPrimitiveCount = Segment.NumPrimitives;
	RayTracingGeometries.Add(RHICreateRayTracingGeometry(GeometryInitializer));
	RayTracingTransforms.Add(PrimitiveSceneProxy->GetLocalToWorld());
}

BEGIN_SHADER_PARAMETER_STRUCT(FBuildBLASPassParams, )
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer, InstanceBuffer)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FBuildTLASPassParams, )
	RDG_BUFFER_ACCESS(RayTracingSceneScratchBuffer, ERHIAccess::UAVCompute)
	RDG_BUFFER_ACCESS(RayTracingSceneInstanceBuffer, ERHIAccess::SRVCompute)
END_SHADER_PARAMETER_STRUCT()

void GenerateRayTracingScene(
	FRDGBuilder& GraphBuilder,
	// Scene data
	const FScene* Scene,
	const FViewInfo& View,
	// Object data
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	// Ray tracing data
	TArray<FRayTracingGeometryRHIRef>& RayTracingGeometries,
	TArray<FMatrix>& RayTracingTransforms,
	// Output
	FRayTracingScene& RayTracingScene
)
{
	RayTracingScene.Reset();

	// Collect instances
	TArray<FRayTracingGeometryInstance> RayTracingInstances;
	for (int32 GeometryIndex = 0; GeometryIndex < RayTracingGeometries.Num(); ++GeometryIndex)
	{
		checkf(RayTracingGeometries[GeometryIndex], TEXT("RayTracingGeometryInstance not created."))
			FRayTracingGeometryInstance RayTracingGeometryInstance = {};
		RayTracingGeometryInstance.GeometryRHI = RayTracingGeometries[GeometryIndex];
		RayTracingGeometryInstance.NumTransforms = 1;
		RayTracingGeometryInstance.Transforms = MakeArrayView(&RayTracingTransforms[GeometryIndex], 1);

		RayTracingInstances.Add(RayTracingGeometryInstance);
	}

	// Build instance BLAS
	FBuildBLASPassParams* PassParamsBLAS = GraphBuilder.AllocParameters<FBuildBLASPassParams>();

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("BuildTLASInstanceBuffer"),
		PassParamsBLAS,
		ERDGPassFlags::Compute | ERDGPassFlags::NeverCull | ERDGPassFlags::NeverParallel,
		[
			PassParamsBLAS,
			&RayTracingGeometries
		](FRHICommandListImmediate& RHICmdList)
		{
			for (FRayTracingGeometryRHIRef RayTracingGeometry : RayTracingGeometries)
			{
				checkf(RayTracingGeometry, TEXT("RayTracingGeometry not created."));
				RHICmdList.BuildAccelerationStructure(RayTracingGeometry);

				// #yuriy_todo: explicit transitions and state validation for BLAS
				// RHICmdList.Transition(FRHITransitionInfo(RayTracingGeometry.GetReference(), ERHIAccess::BVHWrite, ERHIAccess::BVHRead));
			}
		}
	);

	// Create RayTracingScene
	const FGPUScene* EmptyGPUScene = nullptr;
	FViewMatrices EmptyViewMatrices;
	RayTracingScene.Instances = RayTracingInstances;
	RayTracingScene.Create(GraphBuilder, EmptyGPUScene, EmptyViewMatrices);

	// Build TLAS
	FBuildTLASPassParams* PassParamsTLAS = GraphBuilder.AllocParameters<FBuildTLASPassParams>();
	PassParamsTLAS->RayTracingSceneScratchBuffer = RayTracingScene.BuildScratchBuffer;
	PassParamsTLAS->RayTracingSceneInstanceBuffer = RayTracingScene.InstanceBuffer;

	const bool bRayTracingAsyncBuild = false;//CVarRayTracingAsyncBuild.GetValueOnRenderThread() != 0 && GRHISupportsRayTracingAsyncBuildAccelerationStructure;
	const ERDGPassFlags ComputePassFlags = bRayTracingAsyncBuild ? ERDGPassFlags::AsyncCompute : ERDGPassFlags::Compute;
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("RayTracingScene"),
		PassParamsTLAS,
		ComputePassFlags | ERDGPassFlags::NeverCull | ERDGPassFlags::NeverParallel,
		[
			PassParamsTLAS,
			&RayTracingScene,
			bRayTracingAsyncBuild
		](FRHIComputeCommandList& RHICmdList)
		{
			FRHIRayTracingScene* RayTracingSceneRHI = RayTracingScene.GetRHIRayTracingSceneChecked();
			FRHIBuffer* AccelerationStructureBuffer = RayTracingScene.GetBufferChecked();

			FRayTracingSceneBuildParams SceneBuildParams;
			SceneBuildParams.Scene = RayTracingSceneRHI;
			SceneBuildParams.ScratchBuffer = PassParamsTLAS->RayTracingSceneScratchBuffer->GetRHI();
			SceneBuildParams.ScratchBufferOffset = 0;
			SceneBuildParams.InstanceBuffer = PassParamsTLAS->RayTracingSceneInstanceBuffer->GetRHI();
			SceneBuildParams.InstanceBufferOffset = 0;

			RHICmdList.BindAccelerationStructureMemory(RayTracingSceneRHI, AccelerationStructureBuffer, 0);
			RHICmdList.BuildAccelerationStructure(SceneBuildParams);
			// Submit potentially expensive BVH build commands to the GPU as soon as possible.
			// Avoids a GPU bubble in some CPU-limited cases.
			RHICmdList.SubmitCommandsHint();

			RHICmdList.Transition(FRHITransitionInfo(RayTracingSceneRHI, ERHIAccess::BVHWrite, ERHIAccess::BVHRead));
		}
	);
}

class FHeterogeneousVolumesSparseVoxelsHitGroup : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHeterogeneousVolumesSparseVoxelsHitGroup)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FHeterogeneousVolumesSparseVoxelsHitGroup, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSparseVoxelUniformBufferParameters, SparseVoxelUniformBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform) && DoesPlatformSupportHeterogeneousVolumes(Parameters.Platform) &&
			FDataDrivenShaderPlatformInfo::GetSupportsRayTracingProceduralPrimitive(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHeterogeneousVolumesSparseVoxelsHitGroup, "/Engine/Private/HeterogeneousVolumes/HeterogeneousVolumesHardwareRayTracing.usf", "closesthit=SparseVoxelsClosestHitShader anyhit=SparseVoxelsAnyHitShader intersection=SparseVoxelsIntersectionShader", SF_RayHitGroup);

class FHeterogeneousVolumesSparseVoxelMS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHeterogeneousVolumesSparseVoxelMS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FHeterogeneousVolumesSparseVoxelMS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform) && DoesPlatformSupportHeterogeneousVolumes(Parameters.Platform) &&
			FDataDrivenShaderPlatformInfo::GetSupportsRayTracingProceduralPrimitive(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	using FParameters = FEmptyShaderParameters;
};

IMPLEMENT_GLOBAL_SHADER(FHeterogeneousVolumesSparseVoxelMS, "/Engine/Private/HeterogeneousVolumes/HeterogeneousVolumesHardwareRayTracing.usf", "SparseVoxelsMissShader", SF_RayMiss);

class FRenderSingleScatteringWithPreshadingRGS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRenderSingleScatteringWithPreshadingRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FRenderSingleScatteringWithPreshadingRGS, FGlobalShader)

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// Scene 
		SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)

		// Lighting data
		SHADER_PARAMETER(int, bApplyEmission)
		SHADER_PARAMETER(int, bApplyDirectLighting)
		SHADER_PARAMETER(int, LightType)
		SHADER_PARAMETER_STRUCT_REF(FDeferredLightUniformStruct, DeferredLight)

		// Sparse Volume
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSparseVoxelUniformBufferParameters, SparseVoxelUniformBuffer)

		// Volume
		SHADER_PARAMETER(int, MipLevel)

		// Ray
		SHADER_PARAMETER(float, MaxTraceDistance)
		SHADER_PARAMETER(int, MaxStepCount)
		SHADER_PARAMETER(int, bJitter)

		// Output
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWLightingTexture)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform) && DoesPlatformSupportHeterogeneousVolumes(Parameters.Platform) &&
			FDataDrivenShaderPlatformInfo::GetSupportsRayTracingProceduralPrimitive(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FRenderSingleScatteringWithPreshadingRGS, "/Engine/Private/HeterogeneousVolumes/HeterogeneousVolumesHardwareRayTracing.usf", "RenderSingleScatteringWithPreshadingRGS", SF_RayGen);

FRayTracingLocalShaderBindings* BuildRayTracingMaterialBindings(
	FRHICommandList& RHICmdList,
	const FViewInfo& View,
	FRHIUniformBuffer* UniformBuffer
)
{
	auto Alloc = [&](uint32 Size, uint32 Align)
	{
		return RHICmdList.Bypass()
			? FMemStack::Get().Alloc(Size, Align)
			: RHICmdList.Alloc(Size, Align);
	};

	// Allocate bindings
	const uint32 NumBindings = 1;
	FRayTracingLocalShaderBindings* Bindings = (FRayTracingLocalShaderBindings*)Alloc(sizeof(FRayTracingLocalShaderBindings) * NumBindings, alignof(FRayTracingLocalShaderBindings));

	// Allocate and assign uniform buffers
	const uint32 NumUniformBuffers = 1;
	FRHIUniformBuffer** UniformBufferArray = (FRHIUniformBuffer**)Alloc(sizeof(FRHIUniformBuffer*) * NumUniformBuffers, alignof(FRHIUniformBuffer*));
	UniformBufferArray[0] = UniformBuffer;

	// Fill bindings
	for (uint32 BindingIndex = 0; BindingIndex < NumBindings; ++BindingIndex)
	{
		// TODO: Declare useful user-data??
		uint32 UserData = 0;

		FRayTracingLocalShaderBindings Binding = {};
		Binding.InstanceIndex = 0;
		Binding.SegmentIndex = 0;
		Binding.UserData = UserData;
		Binding.UniformBuffers = UniformBufferArray;
		Binding.NumUniformBuffers = NumUniformBuffers;

		Bindings[BindingIndex] = Binding;
	}

	return Bindings;
}

FRayTracingPipelineState* BuildRayTracingPipelineState(
	FRHICommandList& RHICmdList,
	const FViewInfo& View,
	FRHIRayTracingShader* RayGenerationShader
)
{
	FRayTracingPipelineStateInitializer Initializer;
	Initializer.MaxPayloadSizeInBytes = 32; // sizeof FSparseVoxelPayload

	// Get the ray tracing materials
	auto HitGroupShaders = View.ShaderMap->GetShader<FHeterogeneousVolumesSparseVoxelsHitGroup>();
	FRHIRayTracingShader* HitShaderTable[] = {
		HitGroupShaders.GetRayTracingShader()
	};
	Initializer.SetHitGroupTable(HitShaderTable);
	// WARNING: Currently hit-group indexing is required to bind uniform buffers to hit-group shaders.
	Initializer.bAllowHitGroupIndexing = true;

	auto MissShader = View.ShaderMap->GetShader<FHeterogeneousVolumesSparseVoxelMS>();
	FRHIRayTracingShader* MissShaderTable[] = {
		MissShader.GetRayTracingShader()
	};
	Initializer.SetMissShaderTable(MissShaderTable);

	FRHIRayTracingShader* RayGenShaderTable[] = {
		RayGenerationShader
	};
	Initializer.SetRayGenShaderTable(RayGenShaderTable);

	FRayTracingPipelineState* RayTracingPipelineState = PipelineStateCache::GetAndOrCreateRayTracingPipelineState(RHICmdList, Initializer);

	return RayTracingPipelineState;
}

void RenderSingleScatteringWithPreshadingHardwareRayTracing(
	FRDGBuilder& GraphBuilder,
	// Scene data
	const FScene* Scene,
	const FViewInfo& View,
	const FSceneTextures& SceneTextures,
	// Light data
	bool bApplyEmission,
	bool bApplyDirectLighting,
	uint32 LightType,
	const FLightSceneInfo* LightSceneInfo,
	// Object data
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	// Sparse voxel data
	TRDGUniformBufferRef<FSparseVoxelUniformBufferParameters> SparseVoxelUniformBuffer,
	// Ray tracing data
	FRayTracingScene& RayTracingScene,
	// Output
	FRDGTextureRef& HeterogeneousVolumeTexture
)
{
	FRenderSingleScatteringWithPreshadingRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRenderSingleScatteringWithPreshadingRGS::FParameters>();
	{
		// Scene
		PassParameters->TLAS = RayTracingScene.GetLayerSRVChecked(ERayTracingSceneLayer::Base);
		PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
		PassParameters->SceneTextures = GetSceneTextureParameters(GraphBuilder, SceneTextures);

		// Light data
		FDeferredLightUniformStruct DeferredLightUniform;
		PassParameters->bApplyEmission = bApplyEmission;
		PassParameters->bApplyDirectLighting = bApplyDirectLighting;
		if (PassParameters->bApplyDirectLighting)
		{
			DeferredLightUniform = GetDeferredLightParameters(View, *LightSceneInfo);
		}
		PassParameters->DeferredLight = CreateUniformBufferImmediate(DeferredLightUniform, UniformBuffer_SingleDraw);
		PassParameters->LightType = LightType;

		// Sparse Voxel data
		PassParameters->SparseVoxelUniformBuffer = SparseVoxelUniformBuffer;

		// Volume data
		PassParameters->MipLevel = HeterogeneousVolumes::GetMipLevel();

		// Ray data
		PassParameters->MaxTraceDistance = HeterogeneousVolumes::GetMaxTraceDistance();
		PassParameters->MaxStepCount = HeterogeneousVolumes::GetMaxStepCount();
		PassParameters->bJitter = HeterogeneousVolumes::ShouldJitter();

		// Output
		PassParameters->RWLightingTexture = GraphBuilder.CreateUAV(HeterogeneousVolumeTexture);
	}

	FRenderSingleScatteringWithPreshadingRGS::FPermutationDomain PermutationVector;
	TShaderRef<FRenderSingleScatteringWithPreshadingRGS> RayGenerationShader = View.ShaderMap->GetShader<FRenderSingleScatteringWithPreshadingRGS>(PermutationVector);
	FIntPoint DispatchResolution = View.ViewRect.Size();

	FString LightName = TEXT("none");
	if (LightSceneInfo != nullptr)
	{
		FSceneRenderer::GetLightNameForDrawEvent(LightSceneInfo->Proxy, LightName);
	}

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("RenderSingleScatteringWithPreshadingRGS (Light = %s) %ux%u", *LightName, DispatchResolution.X, DispatchResolution.Y),
		PassParameters,
		ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
		[
			PassParameters,
			&View,
			&RayTracingScene,
			RayGenerationShader,
			DispatchResolution
		](FRHIRayTracingCommandList& RHICmdList)
		{
			// Set ray-gen bindings
			FRayTracingShaderBindingsWriter GlobalResources;
			SetShaderParameters(GlobalResources, RayGenerationShader, *PassParameters);

			// Create pipeline
			FRayTracingPipelineState* RayTracingPipelineState = BuildRayTracingPipelineState(RHICmdList, View, RayGenerationShader.GetRayTracingShader());

			// Set hit-group bindings
#if 0
			const uint32 NumBindings = 1;
			FRayTracingLocalShaderBindings* Bindings = BuildRayTracingMaterialBindings(RHICmdList, View, PassParameters->SparseVoxelUniformBuffer->GetRHI());
			const bool bCopyDataToInlineStorage = true;
			RHICmdList.SetRayTracingHitGroups(RayTracingScene, RayTracingPipelineState, NumBindings, Bindings, bCopyDataToInlineStorage);
#else
			uint32 InstanceIndex = 0;
			uint32 SegmentIndex = 0;
			uint32 ShaderSlot = 0;
			uint32 HitGroupIndex = 0;
			uint32 LooseParameterDataSize = 0;
			void* LooseParameterData = nullptr;
			uint32 UserData = 0;
			uint32 NumUniformBuffers = 1;
			FRHIUniformBuffer* UniformBuffers[] = {
				PassParameters->SparseVoxelUniformBuffer->GetRHI()
			};
			RHICmdList.SetRayTracingHitGroup(
				RayTracingScene.GetRHIRayTracingSceneChecked(),
				InstanceIndex, SegmentIndex, ShaderSlot,
				RayTracingPipelineState, HitGroupIndex,
				NumUniformBuffers, UniformBuffers,
				LooseParameterDataSize, LooseParameterData,
				UserData
			);

			uint32 ShaderIndexInPipeline = 0;
			RHICmdList.SetRayTracingMissShader(
				RayTracingScene.GetRHIRayTracingSceneChecked(),
				ShaderSlot,
				RayTracingPipelineState,
				ShaderIndexInPipeline,
				NumUniformBuffers,
				UniformBuffers,
				UserData
			);
#endif

			// Dispatch
			RHICmdList.RayTraceDispatch(
				RayTracingPipelineState,
				RayGenerationShader.GetRayTracingShader(),
				RayTracingScene.GetRHIRayTracingSceneChecked(),
				GlobalResources,
				DispatchResolution.X, DispatchResolution.Y);
		}
	);
}

#endif // RHI_RAYTRACING
