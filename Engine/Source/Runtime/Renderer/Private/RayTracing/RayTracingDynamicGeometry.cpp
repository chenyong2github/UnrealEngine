// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshMaterialShader.h"
#include "ScenePrivate.h"
#include "RayTracingDynamicGeometryCollection.h"

#if RHI_RAYTRACING

DECLARE_CYCLE_STAT(TEXT("RTDynGeomDispatch"), STAT_CLM_RTDynGeomDispatch, STATGROUP_ParallelCommandListMarkers);
DECLARE_CYCLE_STAT(TEXT("RTDynGeomBuild"), STAT_CLM_RTDynGeomBuild, STATGROUP_ParallelCommandListMarkers);

// Workaround for outstanding memory corruption on some platforms when parallel command list translation is used.
#define USE_RAY_TRACING_DYNAMIC_GEOMETRY_PARALLEL_COMMAND_LISTS 0

static bool IsSupportedDynamicVertexFactoryType(const FVertexFactoryType* VertexFactoryType)
{
	return VertexFactoryType == FindVertexFactoryType(FName(TEXT("FNiagaraSpriteVertexFactory"), FNAME_Find))
		|| VertexFactoryType == FindVertexFactoryType(FName(TEXT("FNiagaraSpriteVertexFactoryEx"), FNAME_Find))
		|| VertexFactoryType == FindVertexFactoryType(FName(TEXT("FNiagaraRibbonVertexFactory"), FNAME_Find))
		|| VertexFactoryType == FindVertexFactoryType(FName(TEXT("FLocalVertexFactory"), FNAME_Find))
		|| VertexFactoryType == FindVertexFactoryType(FName(TEXT("FLandscapeVertexFactory"), FNAME_Find))
		|| VertexFactoryType == FindVertexFactoryType(FName(TEXT("FLandscapeFixedGridVertexFactory"), FNAME_Find))
		|| VertexFactoryType == FindVertexFactoryType(FName(TEXT("FLandscapeXYOffsetVertexFactory"), FNAME_Find))
		|| VertexFactoryType == FindVertexFactoryType(FName(TEXT("FGPUSkinPassthroughVertexFactory"), FNAME_Find))
		|| VertexFactoryType == FindVertexFactoryType(FName(TEXT("FInstancedStaticMeshVertexFactory"), FNAME_Find));
}

class FRayTracingDynamicGeometryConverterCS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FRayTracingDynamicGeometryConverterCS, MeshMaterial);
public:
	FRayTracingDynamicGeometryConverterCS(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		PassUniformBuffer.Bind(Initializer.ParameterMap, FSceneTextureUniformParameters::StaticStructMetadata.GetShaderVariableName());

		RWVertexPositions.Bind(Initializer.ParameterMap, TEXT("VertexPositions"));
		UsingIndirectDraw.Bind(Initializer.ParameterMap, TEXT("UsingIndirectDraw"));
		NumVertices.Bind(Initializer.ParameterMap, TEXT("NumVertices"));
		MinVertexIndex.Bind(Initializer.ParameterMap, TEXT("MinVertexIndex"));
		PrimitiveId.Bind(Initializer.ParameterMap, TEXT("PrimitiveId"));
		OutputVertexBaseIndex.Bind(Initializer.ParameterMap, TEXT("OutputVertexBaseIndex"));
		bApplyWorldPositionOffset.Bind(Initializer.ParameterMap, TEXT("bApplyWorldPositionOffset"));
		InstanceTransform.Bind(Initializer.ParameterMap, TEXT("InstanceTransform"));
		InverseTransform.Bind(Initializer.ParameterMap, TEXT("InverseTransform"));
	}

	FRayTracingDynamicGeometryConverterCS() = default;

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return IsSupportedDynamicVertexFactoryType(Parameters.VertexFactoryType) && ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshPassProcessorRenderState& DrawRenderState,
		const FMeshMaterialShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const
	{
		FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, DrawRenderState, ShaderElementData, ShaderBindings);
	}

	void GetElementShaderBindings(
		const FShaderMapPointerTable& PointerTable,
		const FScene* Scene,
		const FSceneView* ViewIfDynamicMeshCommand,
		const FVertexFactory* VertexFactory,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMeshBatch& MeshBatch, 
		const FMeshBatchElement& BatchElement,
		const FMeshMaterialShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams) const
	{
		FMeshMaterialShader::GetElementShaderBindings(PointerTable, Scene, ViewIfDynamicMeshCommand, VertexFactory, InputStreamType, FeatureLevel, PrimitiveSceneProxy, MeshBatch, BatchElement, ShaderElementData, ShaderBindings, VertexStreams);
	}

	LAYOUT_FIELD(FRWShaderParameter, RWVertexPositions);
	LAYOUT_FIELD(FShaderParameter, UsingIndirectDraw);
	LAYOUT_FIELD(FShaderParameter, NumVertices);
	LAYOUT_FIELD(FShaderParameter, MinVertexIndex);
	LAYOUT_FIELD(FShaderParameter, PrimitiveId);
	LAYOUT_FIELD(FShaderParameter, bApplyWorldPositionOffset);
	LAYOUT_FIELD(FShaderParameter, OutputVertexBaseIndex);
	LAYOUT_FIELD(FShaderParameter, InstanceTransform);
	LAYOUT_FIELD(FShaderParameter, InverseTransform);
};

IMPLEMENT_MATERIAL_SHADER_TYPE(, FRayTracingDynamicGeometryConverterCS, TEXT("/Engine/Private/RayTracing/RayTracingDynamicMesh.usf"), TEXT("RayTracingDynamicGeometryConverterCS"), SF_Compute);

FRayTracingDynamicGeometryCollection::FRayTracingDynamicGeometryCollection() 
{
}

FRayTracingDynamicGeometryCollection::~FRayTracingDynamicGeometryCollection()
{
	for (FVertexPositionBuffer* Buffer : VertexPositionBuffers)
	{
		delete Buffer;
	}
	VertexPositionBuffers.Empty();
}

void FRayTracingDynamicGeometryCollection::BeginUpdate()
{
	// Clear working arrays - keep max size allocated
	DispatchCommands.Empty(DispatchCommands.Max());
	BuildParams.Empty(BuildParams.Max());
	Segments.Empty(Segments.Max());

	// Vertex buffer data can be immediatly reused the next frame, because it's already 'consumed' for building the AccelerationStructure data
	for (FVertexPositionBuffer* Buffer : VertexPositionBuffers)
	{
		Buffer->UsedSize = 0;
	}

	// Increment generation ID used for validation
	SharedBufferGenerationID++;
}

void FRayTracingDynamicGeometryCollection::AddDynamicMeshBatchForGeometryUpdate(
	const FScene* Scene,
	const FSceneView* View,
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	FRayTracingDynamicGeometryUpdateParams UpdateParams,
	uint32 PrimitiveId
)
{
	FRayTracingGeometry& Geometry = *UpdateParams.Geometry;
	bool bUsingIndirectDraw = UpdateParams.bUsingIndirectDraw;
	uint32 NumMaxVertices = UpdateParams.NumVertices;

	FRWBuffer* RWBuffer = UpdateParams.Buffer;
	uint32 VertexBufferOffset = 0;
	bool bUseSharedVertexBuffer = false;

	// If update params didn't provide a buffer then use a shared vertex position buffer
	if (RWBuffer == nullptr)
	{
		FVertexPositionBuffer* VertexPositionBuffer = nullptr;
		for (FVertexPositionBuffer* Buffer : VertexPositionBuffers)
		{
			if ((Buffer->RWBuffer.NumBytes - Buffer->UsedSize) >= UpdateParams.VertexBufferSize)
			{
				VertexPositionBuffer = Buffer;
				break;
			}
		}

		// Allocate a new buffer?
		if (VertexPositionBuffer == nullptr)
		{
			VertexPositionBuffer = new FVertexPositionBuffer;
			VertexPositionBuffers.Add(VertexPositionBuffer);

			static const uint32 VertexBufferCacheSize = 16 * 1024 * 1024;
			uint32 AllocationSize = FMath::Max(VertexBufferCacheSize, UpdateParams.VertexBufferSize);

			VertexPositionBuffer->RWBuffer.Initialize(sizeof(float), AllocationSize / sizeof(float), PF_R32_FLOAT, BUF_UnorderedAccess | BUF_ShaderResource, TEXT("FRayTracingDynamicGeometryCollection::RayTracingDynamicVertexBuffer"));
			VertexPositionBuffer->UsedSize = 0;
		}

		// Get the offset and update used size
		VertexBufferOffset = VertexPositionBuffer->UsedSize;
		VertexPositionBuffer->UsedSize += UpdateParams.VertexBufferSize;

		bUseSharedVertexBuffer = true;
		RWBuffer = &VertexPositionBuffer->RWBuffer;
	}

	FMatrix InstanceTransform = UpdateParams.InstanceTransform;
	FMatrix InverseTransform = InstanceTransform;
	InverseTransform.M[3][3] = 1.0f;
	InverseTransform = InverseTransform.InverseFast();

	for (const FMeshBatch& MeshBatch : UpdateParams.MeshBatches)
	{
		const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
		const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(Scene->GetFeatureLevel(), FallbackMaterialRenderProxyPtr);
		auto* MaterialInterface = Material.GetMaterialInterface();
		const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxyPtr ? *FallbackMaterialRenderProxyPtr : *MeshBatch.MaterialRenderProxy;

		TMeshProcessorShaders<
			FMeshMaterialShader,
			FMeshMaterialShader,
			FMeshMaterialShader,
			FMeshMaterialShader,
			FMeshMaterialShader,
			FMeshMaterialShader,
			FRayTracingDynamicGeometryConverterCS> Shaders;

		FMeshComputeDispatchCommand DispatchCmd;

		TShaderRef<FRayTracingDynamicGeometryConverterCS> Shader = Material.GetShader<FRayTracingDynamicGeometryConverterCS>(MeshBatch.VertexFactory->GetType());
		DispatchCmd.MaterialShader = Shader;
		FMeshDrawShaderBindings& ShaderBindings = DispatchCmd.ShaderBindings;

		Shaders.ComputeShader = Shader;
		ShaderBindings.Initialize(Shaders.GetUntypedShaders());

		FMeshMaterialShaderElementData ShaderElementData;
		ShaderElementData.InitializeMeshMaterialData(View, PrimitiveSceneProxy, MeshBatch, -1, false);

		int32 DataOffset = 0;
		FMeshDrawSingleShaderBindings SingleShaderBindings = ShaderBindings.GetSingleShaderBindings(SF_Compute, DataOffset);
		FMeshPassProcessorRenderState DrawRenderState(Scene->UniformBuffers.ViewUniformBuffer);
		Shader->GetShaderBindings(Scene, Scene->GetFeatureLevel(), PrimitiveSceneProxy, MaterialRenderProxy, Material, DrawRenderState, ShaderElementData, SingleShaderBindings);

		FVertexInputStreamArray DummyArray;
		FMeshMaterialShader::GetElementShaderBindings(Shader, Scene, View, MeshBatch.VertexFactory, EVertexInputStreamType::Default, Scene->GetFeatureLevel(), PrimitiveSceneProxy, MeshBatch, MeshBatch.Elements[0], ShaderElementData, SingleShaderBindings, DummyArray);

		DispatchCmd.TargetBuffer = RWBuffer;
		DispatchCmd.NumMaxVertices = UpdateParams.NumVertices;

		// Setup the loose parameters directly on the binding
		uint32 OutputVertexBaseIndex = VertexBufferOffset / sizeof(float);
		uint32 MinVertexIndex = MeshBatch.Elements[0].MinVertexIndex;
		uint32 NumCPUVertices = UpdateParams.NumVertices;
		if (MeshBatch.Elements[0].MinVertexIndex < MeshBatch.Elements[0].MaxVertexIndex)
		{
			NumCPUVertices = 1 + MeshBatch.Elements[0].MaxVertexIndex - MeshBatch.Elements[0].MinVertexIndex;
		}

		const uint32 VertexBufferNumElements = UpdateParams.VertexBufferSize / sizeof(FVector) - MinVertexIndex;
		if (!ensureMsgf(NumCPUVertices <= VertexBufferNumElements, 
			TEXT("Vertex buffer contains %d vertices, but RayTracingDynamicGeometryConverterCS dispatch command expects at least %d."),
			VertexBufferNumElements, NumCPUVertices))
		{
			NumCPUVertices = VertexBufferNumElements;
		}

		SingleShaderBindings.Add(Shader->UsingIndirectDraw, bUsingIndirectDraw ? 1 : 0);
		SingleShaderBindings.Add(Shader->NumVertices, NumCPUVertices);
		SingleShaderBindings.Add(Shader->MinVertexIndex, MinVertexIndex);
		SingleShaderBindings.Add(Shader->PrimitiveId, PrimitiveId);
		SingleShaderBindings.Add(Shader->OutputVertexBaseIndex, OutputVertexBaseIndex);
		SingleShaderBindings.Add(Shader->bApplyWorldPositionOffset, UpdateParams.bApplyWorldPositionOffset ? 1 : 0);
		SingleShaderBindings.Add(Shader->InstanceTransform, InstanceTransform);
		SingleShaderBindings.Add(Shader->InverseTransform, InverseTransform);

#if MESH_DRAW_COMMAND_DEBUG_DATA
		FMeshProcessorShaders ShadersForDebug = Shaders.GetUntypedShaders();
		ShaderBindings.Finalize(&ShadersForDebug);
#endif

		DispatchCommands.Add(DispatchCmd);
	}

	bool bRefit = true;

	// Optionally resize the buffer when not shared (could also be lazy allocated and still empty)
	if (!bUseSharedVertexBuffer && RWBuffer->NumBytes != UpdateParams.VertexBufferSize)
	{
		RWBuffer->Initialize(sizeof(float), UpdateParams.VertexBufferSize / sizeof(float), PF_R32_FLOAT, BUF_UnorderedAccess | BUF_ShaderResource, TEXT("FRayTracingDynamicGeometryCollection::RayTracingDynamicVertexBuffer"));
		bRefit = false;
	}

	if (!Geometry.RayTracingGeometryRHI.IsValid())
	{
		bRefit = false;
	}

	if (!Geometry.Initializer.bAllowUpdate)
	{
		bRefit = false;
	}

	check(Geometry.IsInitialized());

	if (Geometry.Initializer.TotalPrimitiveCount != UpdateParams.NumTriangles)
	{
		check(Geometry.Initializer.Segments.Num() <= 1);
		Geometry.Initializer.TotalPrimitiveCount = UpdateParams.NumTriangles;
		Geometry.Initializer.Segments.Empty();
		FRayTracingGeometrySegment Segment;
		Segment.NumPrimitives = UpdateParams.NumTriangles;
		Geometry.Initializer.Segments.Add(Segment);
		bRefit = false;
	}

	for (FRayTracingGeometrySegment& Segment : Geometry.Initializer.Segments)
	{
		Segment.VertexBuffer = RWBuffer->Buffer;
		Segment.VertexBufferOffset = VertexBufferOffset;
	}

	if (!bRefit)
	{
		checkf(Geometry.Initializer.OfflineData == nullptr, TEXT("Dynamic geometry is not expected to have offline acceleration structure data"));
		Geometry.RayTracingGeometryRHI = RHICreateRayTracingGeometry(Geometry.Initializer);
	}

	FAccelerationStructureBuildParams Params;
	Params.Geometry = Geometry.RayTracingGeometryRHI;
	Params.BuildMode = bRefit
		? EAccelerationStructureBuildMode::Update
		: EAccelerationStructureBuildMode::Build;

	if (bUseSharedVertexBuffer)
	{
		// Make render thread side temporary copy and move to rhi side allocation when command list is known
		// Cache the count of segments so final views can be made when all segments are collected (Segments array could still be reallocated)
		Segments.Append(Geometry.Initializer.Segments);
		Params.Segments = MakeArrayView((FRayTracingGeometrySegment*)nullptr, Geometry.Initializer.Segments.Num());
	}

	BuildParams.Add(Params);
	
	if (bUseSharedVertexBuffer)
	{
		Geometry.DynamicGeometrySharedBufferGenerationID = SharedBufferGenerationID;
	}
	else
	{
		Geometry.DynamicGeometrySharedBufferGenerationID = FRayTracingGeometry::NonSharedVertexBuffers;
	}
}

void FRayTracingDynamicGeometryCollection::DispatchUpdates(FRHIComputeCommandList& ParentCmdList)
{
#if WANTS_DRAW_MESH_EVENTS
#define SCOPED_DRAW_OR_COMPUTE_EVENT(ParentCmdList, Name) FDrawEvent PREPROCESSOR_JOIN(Event_##Name,__LINE__); if(GetEmitDrawEvents()) PREPROCESSOR_JOIN(Event_##Name,__LINE__).Start(ParentCmdList, FColor(0), TEXT(#Name));
#else
#define SCOPED_DRAW_OR_COMPUTE_EVENT(...)
#endif

	if (DispatchCommands.Num() > 0)
	{
		SCOPED_DRAW_OR_COMPUTE_EVENT(ParentCmdList, RayTracingDynamicGeometryUpdate)

		{
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(SortDispatchCommands);

				// This can be optimized by using sorted insert or using map on shaders
				// There are only a handful of unique shaders and a few target buffers so we want to swap state as little as possible
				// to reduce RHI thread overhead
				DispatchCommands.Sort([](const FMeshComputeDispatchCommand& InLHS, const FMeshComputeDispatchCommand& InRHS)
					{
						if (InLHS.MaterialShader.GetComputeShader() != InRHS.MaterialShader.GetComputeShader())
							return InLHS.MaterialShader.GetComputeShader() < InRHS.MaterialShader.GetComputeShader();

						return InLHS.TargetBuffer < InRHS.TargetBuffer;
					});
			}

			{
				TRACE_CPUPROFILER_EVENT_SCOPE(SetupSegmentData);

				// Setup the array views on final allocated segments array
				FRayTracingGeometrySegment* SegmentData = Segments.GetData();
				for (FAccelerationStructureBuildParams& Param : BuildParams)
				{
					uint32 SegmentCount = Param.Segments.Num();
					if (SegmentCount > 0)
					{
						Param.Segments = MakeArrayView(SegmentData, SegmentCount);
						SegmentData += SegmentCount;
					}
				}
			}

			FMemMark Mark(FMemStack::Get());

			TArray<FRHITransitionInfo, TMemStackAllocator<>> TransitionsBefore, TransitionsAfter;
			TArray<FRHIUnorderedAccessView*, TMemStackAllocator<>> OverlapUAVs;
			TransitionsBefore.Reserve(DispatchCommands.Num());
			TransitionsAfter.Reserve(DispatchCommands.Num());
			OverlapUAVs.Reserve(DispatchCommands.Num());
			const FRWBuffer* LastBuffer = nullptr;
			for (FMeshComputeDispatchCommand& Cmd : DispatchCommands)
			{
				if (Cmd.TargetBuffer == nullptr)
				{
					continue;
				}
				FRHIUnorderedAccessView* UAV = Cmd.TargetBuffer->UAV.GetReference();

				// The list is sorted by TargetBuffer, so we can remove duplicates by simply looking at the previous value we've processed.
				if (LastBuffer == Cmd.TargetBuffer)
				{
					// This UAV is used by more than one dispatch, so tell the RHI it's OK to overlap the dispatches, because
					// we're updating disjoint regions.
					if (OverlapUAVs.Num() == 0 || OverlapUAVs.Last() != UAV)
					{
						OverlapUAVs.Add(UAV);
					}
					continue;
				}

				LastBuffer = Cmd.TargetBuffer;

				// Looks like the resource can get here in either UAVCompute or SRVMask mode, so we'll have to use Unknown until we can have better tracking.
				TransitionsBefore.Add(FRHITransitionInfo(UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
				TransitionsAfter.Add(FRHITransitionInfo(UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask));
			}

			TArray<FRHICommandList*> CommandLists;
			TArray<int32> CmdListNumDraws;
			TArray<FGraphEventRef> CmdListPrerequisites;

			auto AllocateCommandList = [&ParentCmdList, &CommandLists, &CmdListNumDraws, &CmdListPrerequisites]
			(uint32 ExpectedNumDraws, TStatId StatId)->FRHIComputeCommandList&
			{
			#if USE_RAY_TRACING_DYNAMIC_GEOMETRY_PARALLEL_COMMAND_LISTS
				if (ParentCmdList.Bypass())
				{
					return ParentCmdList;
				}
				else
				{
					FRHIComputeCommandList& Result = *CommandLists.Add_GetRef(new FRHICommandList(ParentCmdList.GetGPUMask()));
					Result.ExecuteStat = StatId;
					CmdListNumDraws.Add(ExpectedNumDraws);
					CmdListPrerequisites.AddDefaulted();
					return Result;
				}
			#else // USE_RAY_TRACING_DYNAMIC_GEOMETRY_PARALLEL_COMMAND_LISTS
				return ParentCmdList;
			#endif // USE_RAY_TRACING_DYNAMIC_GEOMETRY_PARALLEL_COMMAND_LISTS
			};

			{
				FRHIComputeCommandList& RHICmdList = AllocateCommandList(DispatchCommands.Num(), GET_STATID(STAT_CLM_RTDynGeomDispatch));

				FRHIComputeShader* CurrentShader = nullptr;
				FRWBuffer* CurrentBuffer = nullptr;

				// Transition to writeable for each cmd list and enable UAV overlap, because several dispatches can update non-overlapping portions of the same buffer.
				RHICmdList.Transition(TransitionsBefore);
				RHICmdList.BeginUAVOverlap(OverlapUAVs);

				// Cache the bound uniform buffers because a lot are the same between dispatches
				FShaderBindingState ShaderBindingState;

				FUniformBufferRHIRef PassUniformBuffer = CreateSceneTextureUniformBufferDependentOnShadingPath(RHICmdList, ERHIFeatureLevel::SM5, ESceneTextureSetupMode::None);
				FUniformBufferStaticBindings GlobalUniformBuffers(PassUniformBuffer);
				RHICmdList.SetGlobalUniformBuffers(GlobalUniformBuffers);

				for (FMeshComputeDispatchCommand& Cmd : DispatchCommands)
				{
					const TShaderRef<FRayTracingDynamicGeometryConverterCS>& Shader = Cmd.MaterialShader;
					FRHIComputeShader* ComputeShader = Shader.GetComputeShader();
					if (CurrentShader != ComputeShader)
					{
						RHICmdList.SetComputeShader(ComputeShader);
						CurrentBuffer = nullptr;
						CurrentShader = ComputeShader;

						// Reset binding state
						ShaderBindingState = FShaderBindingState();
					}

					FRWBuffer* TargetBuffer = Cmd.TargetBuffer;
					if (CurrentBuffer != TargetBuffer)
					{
						CurrentBuffer = TargetBuffer;
						Shader->RWVertexPositions.SetBuffer(RHICmdList, CurrentShader, *Cmd.TargetBuffer);
					}

					Cmd.ShaderBindings.SetOnCommandList(RHICmdList, ComputeShader, &ShaderBindingState);
					RHICmdList.DispatchComputeShader(FMath::DivideAndRoundUp<uint32>(Cmd.NumMaxVertices, 64), 1, 1);
				}

				// Make sure buffers are readable again and disable UAV overlap.
				RHICmdList.EndUAVOverlap(OverlapUAVs);
				RHICmdList.Transition(TransitionsAfter);
			}

			{
				FRHIComputeCommandList& RHICmdList = AllocateCommandList(1, GET_STATID(STAT_CLM_RTDynGeomBuild));

				SCOPED_DRAW_OR_COMPUTE_EVENT(RHICmdList, Build);
				RHICmdList.BuildAccelerationStructures(BuildParams);
			}

			// Need to kick parallel translate command lists?
			if (CommandLists.Num() > 0)
			{
				ParentCmdList.QueueParallelAsyncCommandListSubmit(
					CmdListPrerequisites.GetData(), // AnyThreadCompletionEvents
					false,  // bIsPrepass
					CommandLists.GetData(), //CmdLists
					CmdListNumDraws.GetData(), // NumDrawsIfKnown
					CommandLists.Num(), // Num
					0, // MinDrawsPerTranslate
					false // bSpewMerge
				);
			}
		}
	}
		
#undef SCOPED_DRAW_OR_COMPUTE_EVENT
}

void FRayTracingDynamicGeometryCollection::EndUpdate(FRHICommandListImmediate& RHICmdList)
{
	// Move ownership to RHI thread for another frame
	RHICmdList.EnqueueLambda([ArrayOwnedByRHIThread = MoveTemp(Segments)](FRHICommandListImmediate&){});
}

#undef USE_RAY_TRACING_DYNAMIC_GEOMETRY_PARALLEL_COMMAND_LISTS

#endif // RHI_RAYTRACING
