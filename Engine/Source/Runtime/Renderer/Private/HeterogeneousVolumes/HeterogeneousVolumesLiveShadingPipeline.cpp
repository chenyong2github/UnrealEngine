// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeterogeneousVolumes.h"

#include "PixelShaderUtils.h"
#include "RayTracingDefinitions.h"
#include "RayTracingInstance.h"
#include "RayTracingInstanceBufferUtil.h"
#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneManagement.h"
#include "VolumetricFog.h"

class FRenderTransmittanceVolumeWithLiveShadingCS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FRenderTransmittanceVolumeWithLiveShadingCS, MeshMaterial);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// Scene data
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)

		// Light data
		SHADER_PARAMETER(int, bApplyEmissionAndTransmittance)
		SHADER_PARAMETER(int, bApplyDirectLighting)
		SHADER_PARAMETER(int, bApplyShadowTransmittance)
		SHADER_PARAMETER(int, LightType)
		SHADER_PARAMETER_STRUCT_REF(FDeferredLightUniformStruct, DeferredLight)

		// Shadow data
		SHADER_PARAMETER(float, ShadowStepFactor)

		// Object data
		SHADER_PARAMETER(FMatrix44f, LocalToWorld)
		SHADER_PARAMETER(FMatrix44f, WorldToLocal)
		SHADER_PARAMETER(FVector3f, LocalBoundsOrigin)
		SHADER_PARAMETER(FVector3f, LocalBoundsExtent)
		SHADER_PARAMETER(int32, PrimitiveId)

		// Ray data
		SHADER_PARAMETER(float, MaxTraceDistance)
		SHADER_PARAMETER(float, MaxShadowTraceDistance)
		SHADER_PARAMETER(float, StepSize)
		SHADER_PARAMETER(int, MaxStepCount)
		SHADER_PARAMETER(int, bJitter)

		// Volume data
		SHADER_PARAMETER_STRUCT_INCLUDE(FTransmittanceVolumeParameters, TransmittanceVolume)

		// Output
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, RWTransmittanceVolumeTexture)
	END_SHADER_PARAMETER_STRUCT()

	FRenderTransmittanceVolumeWithLiveShadingCS() = default;

	FRenderTransmittanceVolumeWithLiveShadingCS(
		const FMeshMaterialShaderType::CompiledShaderInitializerType & Initializer
	)
		: FMeshMaterialShader(Initializer)
	{
		Bindings.BindForLegacyShaderParameters(
			this,
			Initializer.PermutationId,
			Initializer.ParameterMap,
			*FParameters::FTypeInfo::GetStructMetadata(),
			// Don't require full bindings, we use FMaterialShader::SetParameters
			false
		);
	}

	static bool ShouldCompilePermutation(
		const FMaterialShaderPermutationParameters & Parameters
	)
	{
		return DoesPlatformSupportHeterogeneousVolumes(Parameters.Platform)
			&& (Parameters.MaterialParameters.MaterialDomain == MD_Volume)
			// Restricting compilation to materials bound to Niagara meshes
			&& Parameters.MaterialParameters.bIsUsedWithNiagaraMeshParticles;
	}

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static void ModifyCompilationEnvironment(
		const FMaterialShaderPermutationParameters & Parameters,
		FShaderCompilerEnvironment & OutEnvironment
	)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_1D"), GetThreadGroupSize1D());
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_2D"), GetThreadGroupSize2D());
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_3D"), GetThreadGroupSize3D());

		// This shader takes a very long time to compile with FXC, so we pre-compile it with DXC first and then forward the optimized HLSL to FXC.
		OutEnvironment.CompilerFlags.Add(CFLAG_PrecompileWithDXC);
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);

		OutEnvironment.SetDefine(TEXT("GET_PRIMITIVE_DATA_OVERRIDE"), 1);
	}

	void SetParameters(
		FRHIComputeCommandList & RHICmdList,
		FRHIComputeShader * ShaderRHI,
		const FViewInfo & View,
		const FMaterialRenderProxy * MaterialProxy,
		const FMaterial & Material
	)
	{
		FMaterialShader::SetViewParameters(RHICmdList, ShaderRHI, View, View.ViewUniformBuffer);
		FMaterialShader::SetParameters(RHICmdList, ShaderRHI, MaterialProxy, Material, View);
	}

	static int32 GetThreadGroupSize1D() { return GetThreadGroupSize2D() * GetThreadGroupSize2D(); }
	static int32 GetThreadGroupSize2D() { return 8; }
	static int32 GetThreadGroupSize3D() { return 4; }
};

IMPLEMENT_MATERIAL_SHADER_TYPE(, FRenderTransmittanceVolumeWithLiveShadingCS, TEXT("/Engine/Private/HeterogeneousVolumes/HeterogeneousVolumesLiveShadingPipeline.usf"), TEXT("RenderTransmittanceVolumeWithLiveShadingCS"), SF_Compute);

class FRenderSingleScatteringWithLiveShadingCS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FRenderSingleScatteringWithLiveShadingCS, MeshMaterial);

	class FUseTransmittanceVolume : SHADER_PERMUTATION_BOOL("DIM_USE_TRANSMITTANCE_VOLUME");
	using FPermutationDomain = TShaderPermutationDomain<FUseTransmittanceVolume>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// Scene data
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)

		// Light data
		SHADER_PARAMETER(int, bApplyEmissionAndTransmittance)
		SHADER_PARAMETER(int, bApplyDirectLighting)
		SHADER_PARAMETER(int, bApplyShadowTransmittance)
		SHADER_PARAMETER(int, LightType)
		SHADER_PARAMETER_STRUCT_REF(FDeferredLightUniformStruct, DeferredLight)

		// Shadow data
		SHADER_PARAMETER(float, ShadowStepFactor)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FForwardLightData, ForwardLightData)
		SHADER_PARAMETER_STRUCT_INCLUDE(FVolumeShadowingShaderParameters, VolumeShadowingShaderParameters)

		// Object data
		SHADER_PARAMETER(FMatrix44f, LocalToWorld)
		SHADER_PARAMETER(FMatrix44f, WorldToLocal)
		SHADER_PARAMETER(FVector3f, LocalBoundsOrigin)
		SHADER_PARAMETER(FVector3f, LocalBoundsExtent)
		SHADER_PARAMETER(int32, PrimitiveId)

		// Volume data
		SHADER_PARAMETER_STRUCT_INCLUDE(FTransmittanceVolumeParameters, TransmittanceVolume)

		// Ray data
		SHADER_PARAMETER(float, MaxTraceDistance)
		SHADER_PARAMETER(float, StepSize)
		SHADER_PARAMETER(int, MaxStepCount)
		SHADER_PARAMETER(int, bJitter)

		// Dispatch data
		SHADER_PARAMETER(FIntVector, GroupCount)

		// Output
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWLightingTexture)
		//SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<Volumes::FDebugOutput>, RWDebugOutputBuffer)
	END_SHADER_PARAMETER_STRUCT()

	FRenderSingleScatteringWithLiveShadingCS() = default;

	FRenderSingleScatteringWithLiveShadingCS(
		const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer
	)
		: FMeshMaterialShader(Initializer)
	{
		Bindings.BindForLegacyShaderParameters(
			this,
			Initializer.PermutationId,
			Initializer.ParameterMap,
			*FParameters::FTypeInfo::GetStructMetadata(),
			// Don't require full bindings, we use FMaterialShader::SetParameters
			false
		);
	}

	static bool ShouldCompilePermutation(
		const FMaterialShaderPermutationParameters& Parameters
	)
	{
		return DoesPlatformSupportHeterogeneousVolumes(Parameters.Platform)
			&& (Parameters.MaterialParameters.MaterialDomain == MD_Volume)
			&& Parameters.MaterialParameters.bIsUsedWithNiagaraMeshParticles;
	}

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static void ModifyCompilationEnvironment(
		const FMaterialShaderPermutationParameters& Parameters,
		FShaderCompilerEnvironment& OutEnvironment
	)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_1D"), GetThreadGroupSize1D());
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_2D"), GetThreadGroupSize2D());

		// This shader takes a very long time to compile with FXC, so we pre-compile it with DXC first and then forward the optimized HLSL to FXC.
		OutEnvironment.CompilerFlags.Add(CFLAG_PrecompileWithDXC);
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);

		OutEnvironment.SetDefine(TEXT("GET_PRIMITIVE_DATA_OVERRIDE"), 1);
	}

	void SetParameters(
		FRHIComputeCommandList& RHICmdList,
		FRHIComputeShader* ShaderRHI,
		const FViewInfo& View,
		const FMaterialRenderProxy* MaterialProxy,
		const FMaterial& Material
	)
	{
		FMaterialShader::SetViewParameters(RHICmdList, ShaderRHI, View, View.ViewUniformBuffer);
		FMaterialShader::SetParameters(RHICmdList, ShaderRHI, MaterialProxy, Material, View);
	}

	static int32 GetThreadGroupSize1D() { return GetThreadGroupSize2D() * GetThreadGroupSize2D(); }
	static int32 GetThreadGroupSize2D() { return 8; }
};

IMPLEMENT_MATERIAL_SHADER_TYPE(, FRenderSingleScatteringWithLiveShadingCS, TEXT("/Engine/Private/HeterogeneousVolumes/HeterogeneousVolumesLiveShadingPipeline.usf"), TEXT("RenderSingleScatteringWithLiveShadingCS"), SF_Compute);

template <typename ComputeShaderType>
void AddComputePass(
	FRDGBuilder& GraphBuilder,
	TShaderRef<ComputeShaderType>& ComputeShader,
	typename ComputeShaderType::FParameters* PassParameters,
	const FScene* Scene,
	const FViewInfo& View,
	const FMaterialRenderProxy* MaterialRenderProxy,
	const FMaterial& Material,
	const FString& PassName,
	FIntVector GroupCount
)
{
	ClearUnusedGraphResources(ComputeShader, PassParameters);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("%s", *PassName),
		PassParameters,
		ERDGPassFlags::Compute,
		[ComputeShader, PassParameters, Scene, MaterialRenderProxy, &Material, GroupCount](FRHIComputeCommandList& RHICmdList)
		{
			FMeshPassProcessorRenderState DrawRenderState;

			FMeshMaterialShaderElementData ShaderElementData;
			ShaderElementData.FadeUniformBuffer = GDistanceCullFadedInUniformBuffer.GetUniformBufferRHI();
			ShaderElementData.DitherUniformBuffer = GDitherFadedInUniformBuffer.GetUniformBufferRHI();

			FMeshProcessorShaders PassShaders;
			PassShaders.ComputeShader = ComputeShader;

			FMeshDrawShaderBindings ShaderBindings;
			{
				ShaderBindings.Initialize(PassShaders);

				int32 DataOffset = 0;
				FMeshDrawSingleShaderBindings SingleShaderBindings = ShaderBindings.GetSingleShaderBindings(SF_Compute, DataOffset);
				ComputeShader->GetShaderBindings(Scene, Scene->GetFeatureLevel(), nullptr, *MaterialRenderProxy, Material, DrawRenderState, ShaderElementData, SingleShaderBindings);

				ShaderBindings.Finalize(&PassShaders);
			}
			ShaderBindings.SetOnCommandList(RHICmdList, ComputeShader.GetComputeShader());

			FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *PassParameters, GroupCount);
		}
	);
}

void RenderTransmittanceVolumeWithLiveShading(
	FRDGBuilder& GraphBuilder,
	// Scene data
	const FScene* Scene,
	const FViewInfo& View,
	const FSceneTextures& SceneTextures,
	// Light data
	bool bApplyEmissionAndTransmittance,
	bool bApplyDirectLighting,
	bool bApplyShadowTransmittance,
	uint32 LightType,
	const FLightSceneInfo* LightSceneInfo,
	// Object data
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	const FMaterialRenderProxy* DefaultMaterialRenderProxy,
	const int32 PrimitiveId,
	const FBoxSphereBounds LocalBoxSphereBounds,
	// Output
	FRDGTextureRef TransmittanceVolumeTexture
)
{
	const FMaterialRenderProxy* MaterialRenderProxy = nullptr;
	const FMaterial& Material = DefaultMaterialRenderProxy->GetMaterialWithFallback(View.GetFeatureLevel(), MaterialRenderProxy);
	MaterialRenderProxy = MaterialRenderProxy ? MaterialRenderProxy : DefaultMaterialRenderProxy;

	check(Material.GetMaterialDomain() == MD_Volume);

	FRenderTransmittanceVolumeWithLiveShadingCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRenderTransmittanceVolumeWithLiveShadingCS::FParameters>();
	{
		// Scene data
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->SceneTextures = GetSceneTextureParameters(GraphBuilder, SceneTextures);

		// Light data
		check(LightSceneInfo != nullptr)
		FDeferredLightUniformStruct DeferredLightUniform = GetDeferredLightParameters(View, *LightSceneInfo);
		PassParameters->DeferredLight = CreateUniformBufferImmediate(DeferredLightUniform, UniformBuffer_SingleDraw);
		PassParameters->LightType = LightType;

		// Object data
		FMatrix44f LocalToWorld = FMatrix44f(PrimitiveSceneProxy->GetLocalToWorld());
		PassParameters->LocalToWorld = LocalToWorld;
		PassParameters->WorldToLocal = LocalToWorld.Inverse();
		PassParameters->LocalBoundsOrigin = FVector3f(LocalBoxSphereBounds.Origin);
		PassParameters->LocalBoundsExtent = FVector3f(LocalBoxSphereBounds.BoxExtent);
		PassParameters->PrimitiveId = PrimitiveId;

		// Transmittance volume
		PassParameters->TransmittanceVolume.TransmittanceVolumeResolution = HeterogeneousVolumes::GetTransmittanceVolumeResolution();
		//PassParameters->TransmittanceVolume.TransmittanceVolumeTexture = GraphBuilder.CreateSRV(TransmittanceVolumeTexture);

		// Ray data
		PassParameters->MaxTraceDistance = HeterogeneousVolumes::GetMaxTraceDistance();
		PassParameters->MaxShadowTraceDistance = HeterogeneousVolumes::GetMaxShadowTraceDistance();
		PassParameters->StepSize = HeterogeneousVolumes::GetStepSize();
		PassParameters->ShadowStepFactor = HeterogeneousVolumes::GetShadowStepFactor();
		PassParameters->MaxStepCount = HeterogeneousVolumes::GetMaxStepCount();
		PassParameters->bJitter = HeterogeneousVolumes::ShouldJitter();

		// Output
		PassParameters->RWTransmittanceVolumeTexture = GraphBuilder.CreateUAV(TransmittanceVolumeTexture);
	}

	FString LightName = TEXT("none");
	if (LightSceneInfo != nullptr)
	{
		FSceneRenderer::GetLightNameForDrawEvent(LightSceneInfo->Proxy, LightName);
	}
	FString PassName = FString::Printf(TEXT("RenderTransmittanceVolumeWithLiveShadingCS (Light = %s)"), *LightName);

	FIntVector GroupCount = HeterogeneousVolumes::GetTransmittanceVolumeResolution();
	GroupCount.X = FMath::DivideAndRoundUp(GroupCount.X, FRenderTransmittanceVolumeWithLiveShadingCS::GetThreadGroupSize3D());
	GroupCount.Y = FMath::DivideAndRoundUp(GroupCount.Y, FRenderTransmittanceVolumeWithLiveShadingCS::GetThreadGroupSize3D());
	GroupCount.Z = FMath::DivideAndRoundUp(GroupCount.Z, FRenderTransmittanceVolumeWithLiveShadingCS::GetThreadGroupSize3D());

	FRenderTransmittanceVolumeWithLiveShadingCS::FPermutationDomain PermutationVector;
	TShaderRef<FRenderTransmittanceVolumeWithLiveShadingCS> ComputeShader = Material.GetShader<FRenderTransmittanceVolumeWithLiveShadingCS>(&FLocalVertexFactory::StaticType, PermutationVector, false);
	if (!ComputeShader.IsNull())
	{
		AddComputePass(GraphBuilder, ComputeShader, PassParameters, Scene, View, MaterialRenderProxy, Material, PassName, GroupCount);
	}
}

void RenderSingleScatteringWithLiveShading(
	FRDGBuilder& GraphBuilder,
	// Scene data
	const FScene* Scene,
	const FViewInfo& View,
	const FSceneTextures& SceneTextures,
	// Light data
	bool bApplyEmissionAndTransmittance,
	bool bApplyDirectLighting,
	bool bApplyShadowTransmittance,
	uint32 LightType,
	const FLightSceneInfo* LightSceneInfo,
	// Shadow data
	const FVisibleLightInfo* VisibleLightInfo,
	// Object data
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	const FMaterialRenderProxy* DefaultMaterialRenderProxy,
	const int32 PrimitiveId,
	const FBoxSphereBounds LocalBoxSphereBounds,
	// Transmittance acceleration
	FRDGTextureRef TransmittanceVolumeTexture,
	// Output
	FRDGTextureRef& HeterogeneousVolumeTexture
)
{
	const FMaterialRenderProxy* MaterialRenderProxy = nullptr;
	const FMaterial& Material = DefaultMaterialRenderProxy->GetMaterialWithFallback(View.GetFeatureLevel(), MaterialRenderProxy);
	MaterialRenderProxy = MaterialRenderProxy ? MaterialRenderProxy : DefaultMaterialRenderProxy;
	check(Material.GetMaterialDomain() == MD_Volume);

	uint32 GroupCountX = FMath::DivideAndRoundUp(View.ViewRect.Size().X, FRenderSingleScatteringWithLiveShadingCS::GetThreadGroupSize2D());
	uint32 GroupCountY = FMath::DivideAndRoundUp(View.ViewRect.Size().Y, FRenderSingleScatteringWithLiveShadingCS::GetThreadGroupSize2D());
	FIntVector GroupCount = FIntVector(GroupCountX, GroupCountY, 1);

#if 0
	FRDGBufferRef DebugOutputBuffer = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(Volumes::FDebugOutput), GroupCountX * GroupCountY * FRenderSingleScatteringWithLiveShadingCS::GetThreadGroupSize2D()),
		TEXT("Lumen.Reflections.TraceDataPacked"));
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(DebugOutputBuffer), 0);
#endif

	FRenderSingleScatteringWithLiveShadingCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRenderSingleScatteringWithLiveShadingCS::FParameters>();
	{
		// Scene data
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->SceneTextures = GetSceneTextureParameters(GraphBuilder, SceneTextures);

		// Light data
		FDeferredLightUniformStruct DeferredLightUniform;
		PassParameters->bApplyEmissionAndTransmittance = bApplyEmissionAndTransmittance;
		PassParameters->bApplyDirectLighting = bApplyDirectLighting;
		PassParameters->bApplyShadowTransmittance = bApplyShadowTransmittance;
		if (PassParameters->bApplyDirectLighting && (LightSceneInfo != nullptr))
		{
			DeferredLightUniform = GetDeferredLightParameters(View, *LightSceneInfo);
		}
		PassParameters->DeferredLight = CreateUniformBufferImmediate(DeferredLightUniform, UniformBuffer_SingleDraw);
		PassParameters->LightType = LightType;
		PassParameters->ShadowStepFactor = HeterogeneousVolumes::GetShadowStepFactor();

		// Object data
		FMatrix44f LocalToWorld = FMatrix44f(PrimitiveSceneProxy->GetLocalToWorld());
		PassParameters->LocalToWorld = LocalToWorld;
		PassParameters->WorldToLocal = LocalToWorld.Inverse();
		PassParameters->LocalBoundsOrigin = FVector3f(LocalBoxSphereBounds.Origin);
		PassParameters->LocalBoundsExtent = FVector3f(LocalBoxSphereBounds.BoxExtent);
		PassParameters->PrimitiveId = PrimitiveId;

		// Ray data
		PassParameters->MaxTraceDistance = HeterogeneousVolumes::GetMaxTraceDistance();
		PassParameters->StepSize = HeterogeneousVolumes::GetStepSize();
		PassParameters->MaxStepCount = HeterogeneousVolumes::GetMaxStepCount();
		PassParameters->bJitter = HeterogeneousVolumes::ShouldJitter();

		// Shadow data
		PassParameters->ForwardLightData = View.ForwardLightingResources.ForwardLightUniformBuffer;
		if (VisibleLightInfo != nullptr)
		{
			const FProjectedShadowInfo* ProjectedShadowInfo = GetShadowForInjectionIntoVolumetricFog(*VisibleLightInfo);
			bool bDynamicallyShadowed = ProjectedShadowInfo != NULL;
			if (bDynamicallyShadowed)
			{
				GetVolumeShadowingShaderParameters(GraphBuilder, View, LightSceneInfo, ProjectedShadowInfo, PassParameters->VolumeShadowingShaderParameters);
			}
			else
			{
				SetVolumeShadowingDefaultShaderParametersGlobal(GraphBuilder, PassParameters->VolumeShadowingShaderParameters);
			}
		}
		else
		{
			SetVolumeShadowingDefaultShaderParametersGlobal(GraphBuilder, PassParameters->VolumeShadowingShaderParameters);
		}

		// Volume data
		if (HeterogeneousVolumes::UseTransmittanceVolume() && bApplyShadowTransmittance)
		{
			PassParameters->TransmittanceVolume.TransmittanceVolumeResolution = HeterogeneousVolumes::GetTransmittanceVolumeResolution();
			//PassParameters->TransmittanceVolume.TransmittanceVolumeTexture = GraphBuilder.CreateSRV(TransmittanceVolumeTexture);
			PassParameters->TransmittanceVolume.TransmittanceVolumeTexture = TransmittanceVolumeTexture;
		}

		// Dispatch data
		PassParameters->GroupCount = GroupCount;

		// Output
		PassParameters->RWLightingTexture = GraphBuilder.CreateUAV(HeterogeneousVolumeTexture);
		//PassParameters->RWDebugOutputBuffer = GraphBuilder.CreateUAV(DebugOutputBuffer);
	}

	FString LightName = TEXT("none");
	if (LightSceneInfo != nullptr)
	{
		FSceneRenderer::GetLightNameForDrawEvent(LightSceneInfo->Proxy, LightName);
	}
	FString PassName = FString::Printf(TEXT("RenderSingleScatteringWithLiveShadingCS (Light = %s)"), *LightName);

	FRenderSingleScatteringWithLiveShadingCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FRenderSingleScatteringWithLiveShadingCS::FUseTransmittanceVolume>(HeterogeneousVolumes::UseTransmittanceVolume() && PassParameters->bApplyShadowTransmittance);
	TShaderRef<FRenderSingleScatteringWithLiveShadingCS> ComputeShader = Material.GetShader<FRenderSingleScatteringWithLiveShadingCS>(&FLocalVertexFactory::StaticType, PermutationVector, false);
	if (!ComputeShader.IsNull())
	{
		AddComputePass(GraphBuilder, ComputeShader, PassParameters, Scene, View, MaterialRenderProxy, Material, PassName, GroupCount);
	}
}

void RenderWithLiveShading(
	FRDGBuilder& GraphBuilder,
	const FSceneTextures& SceneTextures,
	const FScene* Scene,
	const FViewInfo& View,
	// Shadow data
	TArray<FVisibleLightInfo, SceneRenderingAllocator>& VisibleLightInfos,
	// Object data
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	const FMaterialRenderProxy* MaterialRenderProxy,
	const int32 PrimitiveId,
	const FBoxSphereBounds LocalBoxSphereBounds,
	// Transmittance acceleration
	FRDGTextureRef TransmittanceVolumeTexture,
	// Output
	FRDGTextureRef& HeterogeneousVolumeRadiance
)
{
	// Light culling
	TArray<FLightSceneInfoCompact, TInlineAllocator<64>> LightSceneInfoCompact;
	for (auto LightIt = Scene->Lights.CreateConstIterator(); LightIt; ++LightIt)
	{
		if (LightIt->AffectsPrimitive(PrimitiveSceneProxy->GetBounds(), PrimitiveSceneProxy))
		{
			LightSceneInfoCompact.Add(*LightIt);
		}
	}

	// Light loop:
	int32 NumPasses = FMath::Max(LightSceneInfoCompact.Num(), 1);
	for (int32 PassIndex = 0; PassIndex < NumPasses; ++PassIndex)
	{
		bool bApplyEmissionAndTransmittance = PassIndex == 0;
		bool bApplyDirectLighting = !LightSceneInfoCompact.IsEmpty();
		bool bApplyShadowTransmittance = false;

		uint32 LightType = 0;
		FLightSceneInfo* LightSceneInfo = nullptr;
		const FVisibleLightInfo* VisibleLightInfo = nullptr;
		if (bApplyDirectLighting)
		{
			LightType = LightSceneInfoCompact[PassIndex].LightType;
			LightSceneInfo = LightSceneInfoCompact[PassIndex].LightSceneInfo;
			VisibleLightInfo = &VisibleLightInfos[LightSceneInfo->Id];
			check(LightSceneInfo != nullptr);

			bApplyDirectLighting = (LightSceneInfo != nullptr);
			bApplyShadowTransmittance = LightSceneInfo->Proxy->CastsVolumetricShadow();
		}

		if (HeterogeneousVolumes::UseTransmittanceVolume() && bApplyShadowTransmittance)
		{
			RenderTransmittanceVolumeWithLiveShading(
				GraphBuilder,
				// Scene data
				Scene,
				View,
				SceneTextures,
				// Light data
				bApplyEmissionAndTransmittance,
				bApplyDirectLighting,
				bApplyShadowTransmittance,
				LightType,
				LightSceneInfo,
				// Object data
				PrimitiveSceneProxy,
				MaterialRenderProxy,
				PrimitiveId,
				LocalBoxSphereBounds,
				// Output
				TransmittanceVolumeTexture
			);
		}

		RenderSingleScatteringWithLiveShading(
			GraphBuilder,
			// Scene data
			Scene,
			View,
			SceneTextures,
			// Light data
			bApplyEmissionAndTransmittance,
			bApplyDirectLighting,
			bApplyShadowTransmittance,
			LightType,
			LightSceneInfo,
			// Shadow data
			VisibleLightInfo,
			// Object data
			PrimitiveSceneProxy,
			MaterialRenderProxy,
			PrimitiveId,
			LocalBoxSphereBounds,
			// Transmittance acceleration
			TransmittanceVolumeTexture,
			// Output
			HeterogeneousVolumeRadiance
		);
	}
}