// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MeshMaterialShader.h"
#include "ScenePrivate.h"
#include "RayTracingDynamicGeometryCollection.h"

#if RHI_RAYTRACING

DECLARE_GPU_STAT_NAMED(RayTracingDynamicGeom, TEXT("Ray Tracing Dynamic Geometry Update"));

static bool IsSupportedDynamicVertexFactoryType(const FVertexFactoryType* VertexFactoryType)
{
	return VertexFactoryType == FindVertexFactoryType(FName(TEXT("FNiagaraSpriteVertexFactory"), FNAME_Find))
		|| VertexFactoryType == FindVertexFactoryType(FName(TEXT("FLandscapeVertexFactory"), FNAME_Find))
		|| VertexFactoryType == FindVertexFactoryType(FName(TEXT("FLandscapeXYOffsetVertexFactory"), FNAME_Find))
		|| VertexFactoryType == FindVertexFactoryType(FName(TEXT("FGPUSkinPassthroughVertexFactory"), FNAME_Find));
}

class FRayTracingDynamicGeometryConverterCS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FRayTracingDynamicGeometryConverterCS, MeshMaterial);
public:
	FRayTracingDynamicGeometryConverterCS(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		PassUniformBuffer.Bind(Initializer.ParameterMap, FSceneTexturesUniformParameters::StaticStructMetadata.GetShaderVariableName());

		RWVertexPositions.Bind(Initializer.ParameterMap, TEXT("VertexPositions"));
		VertexBufferSize.Bind(Initializer.ParameterMap, TEXT("VertexBufferSize"));
		NumVertices.Bind(Initializer.ParameterMap, TEXT("NumVertices"));
		MinVertexIndex.Bind(Initializer.ParameterMap, TEXT("MinVertexIndex"));
	}

	FRayTracingDynamicGeometryConverterCS() = default;

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return IsSupportedDynamicVertexFactoryType(Parameters.VertexFactoryType) && ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FMeshMaterialShader::Serialize(Ar);
		Ar << RWVertexPositions;
		Ar << VertexBufferSize;
		Ar << NumVertices;
		Ar << MinVertexIndex;
		return bShaderHasOutdatedParameters;
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
		FMeshMaterialShader::GetElementShaderBindings(Scene, ViewIfDynamicMeshCommand, VertexFactory, InputStreamType, FeatureLevel, PrimitiveSceneProxy, MeshBatch, BatchElement, ShaderElementData, ShaderBindings, VertexStreams);
	}

	FRWShaderParameter RWVertexPositions;
	FShaderParameter VertexBufferSize;
	FShaderParameter NumVertices;
	FShaderParameter MinVertexIndex;
};

IMPLEMENT_MATERIAL_SHADER_TYPE(, FRayTracingDynamicGeometryConverterCS, TEXT("/Engine/Private/RayTracing/RayTracingDynamicMesh.usf"), TEXT("RayTracingDynamicGeometryConverterCS"), SF_Compute);

FRayTracingDynamicGeometryCollection::FRayTracingDynamicGeometryCollection()
{
	DispatchCommands = MakeUnique<TArray<FMeshComputeDispatchCommand>>();
}

void FRayTracingDynamicGeometryCollection::AddDynamicMeshBatchForGeometryUpdate(
	const FScene* Scene, 
	const FSceneView* View, 
	const FPrimitiveSceneProxy* PrimitiveSceneProxy, 
	FRayTracingDynamicGeometryUpdateParams UpdateParams
)
{
	FRayTracingGeometry& Geometry = *UpdateParams.Geometry;
	bool bUsingIndirectDraw = UpdateParams.bUsingIndirectDraw;
	uint32 NumMaxVertices = UpdateParams.NumVertices;
	FRWBuffer& Buffer = *UpdateParams.Buffer;

	for (const FMeshBatch& MeshBatch : UpdateParams.MeshBatches)
	{
		const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
		const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(Scene->GetFeatureLevel(), FallbackMaterialRenderProxyPtr);

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

		FRayTracingDynamicGeometryConverterCS* Shader = Material.GetShader<FRayTracingDynamicGeometryConverterCS>(MeshBatch.VertexFactory->GetType());
		DispatchCmd.MaterialShader = Shader;
		FMeshDrawShaderBindings& ShaderBindings = DispatchCmd.ShaderBindings;

		Shaders.ComputeShader = Shader;
		ShaderBindings.Initialize(Shaders.GetUntypedShaders());

		FMeshMaterialShaderElementData ShaderElementData;
		ShaderElementData.InitializeMeshMaterialData(View, PrimitiveSceneProxy, MeshBatch, -1, false);

		FMeshDrawSingleShaderBindings SingleShaderBindings = ShaderBindings.GetSingleShaderBindings(SF_Compute);
		FMeshPassProcessorRenderState DrawRenderState(Scene->UniformBuffers.ViewUniformBuffer, Scene->UniformBuffers.OpaqueBasePassUniformBuffer);
		Shader->GetShaderBindings(Scene, Scene->GetFeatureLevel(), PrimitiveSceneProxy, MaterialRenderProxy, Material, DrawRenderState, ShaderElementData, SingleShaderBindings);

		FVertexInputStreamArray DummyArray;
		Shader->GetElementShaderBindings(Scene, View, MeshBatch.VertexFactory, EVertexInputStreamType::Default, Scene->GetFeatureLevel(), PrimitiveSceneProxy, MeshBatch, MeshBatch.Elements[0], ShaderElementData, SingleShaderBindings, DummyArray);

		DispatchCmd.TargetBuffer = &Buffer;
		DispatchCmd.NumMaxVertices = UpdateParams.NumVertices;
		DispatchCmd.NumCPUVertices = !bUsingIndirectDraw ? UpdateParams.NumVertices : 0;
		if (MeshBatch.Elements[0].MinVertexIndex < MeshBatch.Elements[0].MaxVertexIndex)
		{
			DispatchCmd.NumCPUVertices = MeshBatch.Elements[0].MaxVertexIndex - MeshBatch.Elements[0].MinVertexIndex;
		}
		DispatchCmd.MinVertexIndex = MeshBatch.Elements[0].MinVertexIndex;

#if MESH_DRAW_COMMAND_DEBUG_DATA
		FMeshProcessorShaders ShadersForDebug = Shaders.GetUntypedShaders();
		ShaderBindings.Finalize(&ShadersForDebug);
#endif

		DispatchCommands->Add(DispatchCmd);
	}

	bool bRefit = true;

	uint32 DesiredVertexBufferSize = UpdateParams.VertexBufferSize;
	if (Buffer.NumBytes != DesiredVertexBufferSize)
	{
		int32 OriginalSize = Buffer.NumBytes;
		Buffer.Initialize(sizeof(float), DesiredVertexBufferSize / sizeof(float), PF_R32_FLOAT, BUF_UnorderedAccess | BUF_ShaderResource, TEXT("RayTracingDynamicVertexBuffer"));
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
	Geometry.Initializer.PositionVertexBuffer = Buffer.Buffer;
	Geometry.Initializer.TotalPrimitiveCount = UpdateParams.NumTriangles;

	if (!bRefit)
	{
		Geometry.RayTracingGeometryRHI = RHICreateRayTracingGeometry(Geometry.Initializer);
		BuildParams.Add(FAccelerationStructureUpdateParams{ Geometry.RayTracingGeometryRHI, Buffer.Buffer });
	}
	else
	{
		RefitParams.Add(FAccelerationStructureUpdateParams{ Geometry.RayTracingGeometryRHI, Buffer.Buffer });
	}
}

void FRayTracingDynamicGeometryCollection::DispatchUpdates(FRHICommandListImmediate& RHICmdList)
{
	if (DispatchCommands->Num() > 0)
	{
		SCOPED_DRAW_EVENT(RHICmdList, RayTracingDynamicGeometryUpdate);
		SCOPED_GPU_STAT(RHICmdList, RayTracingDynamicGeom);

		{
			SCOPED_DRAW_EVENT(RHICmdList, VSinCSComputeDispatch);
			for (auto& Cmd : *DispatchCommands)
			{
				{
					FRayTracingDynamicGeometryConverterCS* Shader = Cmd.MaterialShader;

					RHICmdList.SetComputeShader(Shader->GetComputeShader());

					Cmd.ShaderBindings.SetOnCommandListForCompute(RHICmdList, Shader->GetComputeShader());
					Shader->RWVertexPositions.SetBuffer(RHICmdList, Shader->GetComputeShader(), *Cmd.TargetBuffer);
					SetShaderValue(RHICmdList, Shader->GetComputeShader(), Shader->VertexBufferSize, Cmd.TargetBuffer->NumBytes / sizeof(FVector));
					SetShaderValue(RHICmdList, Shader->GetComputeShader(), Shader->NumVertices, Cmd.NumCPUVertices);
					SetShaderValue(RHICmdList, Shader->GetComputeShader(), Shader->MinVertexIndex, Cmd.MinVertexIndex);

					RHICmdList.DispatchComputeShader(FMath::DivideAndRoundUp<uint32>(Cmd.NumMaxVertices, 256), 1, 1);

					Shader->RWVertexPositions.UnsetUAV(RHICmdList, Shader->GetComputeShader());
				}
			}
		}

		{
			SCOPED_DRAW_EVENT(RHICmdList, Build);
			RHICmdList.BuildAccelerationStructures(BuildParams);
		}

		{
			SCOPED_DRAW_EVENT(RHICmdList, Refit);
			RHICmdList.UpdateAccelerationStructures(RefitParams);
		}

		Clear();
	}
}

void FRayTracingDynamicGeometryCollection::Clear()
{
	DispatchCommands->Empty();
	BuildParams.Empty();
	RefitParams.Empty();
}

#endif // RHI_RAYTRACING
