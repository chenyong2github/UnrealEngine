// Copyright Epic Games, Inc. All Rights Reserved.

#include "RHI.h"
#include "ScenePrivate.h"
#include "ScreenPass.h"

#if RHI_RAYTRACING

#include "DeferredShadingRenderer.h"
#include "GlobalShader.h"
#include "SceneRenderTargets.h"
#include "RenderGraphBuilder.h"
#include "SceneUtils.h"
#include "RaytracingDebugDefinitions.h"
#include "RayTracing/RayTracingLighting.h"
#include "RayTracing/RaytracingOptions.h"
#include "RayTracing/RayTracingTraversalStatistics.h"

#define LOCTEXT_NAMESPACE "RayTracingDebugVisualizationMenuCommands"

DECLARE_GPU_STAT(RayTracingDebug);

static TAutoConsoleVariable<FString> CVarRayTracingDebugMode(
	TEXT("r.RayTracing.DebugVisualizationMode"),
	TEXT(""),
	TEXT("Sets the ray tracing debug visualization mode (default = None - Driven by viewport menu) .\n")
	);

TAutoConsoleVariable<int32> CVarRayTracingDebugModeOpaqueOnly(
	TEXT("r.RayTracing.DebugVisualizationMode.OpaqueOnly"),
	1,
	TEXT("Sets whether the view mode rendes opaque objects only (default = 1, render only opaque objects, 0 = render all objects)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarRayTracingDebugTimingScale(
	TEXT("r.RayTracing.DebugTimingScale"),
	1.0f,
	TEXT("Scaling factor for ray timing heat map visualization. (default = 1)\n")
);

static TAutoConsoleVariable<float> CVarRayTracingDebugTraversalBoxScale(
	TEXT("r.RayTracing.DebugTraversalScale.Box"),
	150.0f,
	TEXT("Scaling factor for box traversal heat map visualization. (default = 150)\n")
);

static TAutoConsoleVariable<float> CVarRayTracingDebugTraversalClusterScale(
	TEXT("r.RayTracing.DebugTraversalScale.Cluster"),
	2500.0f,
	TEXT("Scaling factor for cluster traversal heat map visualization. (default = 2500)\n")
);

static TAutoConsoleVariable<float> CVarRayTracingDebugTraversalTriangleScale(
	TEXT("r.RayTracing.DebugTraversalScale.Triangle"),
	30.0f,
	TEXT("Scaling factor for triangle traversal heat map visualization. (default = 30)\n")
);

static int32 GVisualizeProceduralPrimitives = 0;
static FAutoConsoleVariableRef CVarVisualizeProceduralPrimitives(
	TEXT("r.RayTracing.DebugVisualizationMode.ProceduralPrimitives"),
	GVisualizeProceduralPrimitives,
	TEXT("Whether to include procedural primitives in visualization modes.\n")
	TEXT("Currently only supports Nanite primitives in inline barycentrics mode."),
	ECVF_RenderThreadSafe
);

class FRayTracingDebugRGS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRayTracingDebugRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FRayTracingDebugRGS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, VisualizationMode)
		SHADER_PARAMETER(int32, ShouldUsePreExposure)
		SHADER_PARAMETER(float, TimingScale)
		SHADER_PARAMETER(float, MaxTraceDistance)
		SHADER_PARAMETER(float, FarFieldMaxTraceDistance)
		SHADER_PARAMETER(FVector3f, FarFieldReferencePos)
		SHADER_PARAMETER(int32, OpaqueOnly)
		SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, Output)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}
};
IMPLEMENT_GLOBAL_SHADER(FRayTracingDebugRGS, "/Engine/Private/RayTracing/RayTracingDebug.usf", "RayTracingDebugMainRGS", SF_RayGen);

class FRayTracingDebugCHS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRayTracingDebugCHS);

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	FRayTracingDebugCHS() = default;
	FRayTracingDebugCHS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{}
};
IMPLEMENT_SHADER_TYPE(, FRayTracingDebugCHS, TEXT("/Engine/Private/RayTracing/RayTracingDebug.usf"), TEXT("RayTracingDebugMainCHS"), SF_RayHitGroup);

class FRayTracingDebugTraversalCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRayTracingDebugTraversalCS)
	SHADER_USE_PARAMETER_STRUCT(FRayTracingDebugTraversalCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, Output)
		SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FNaniteUniformParameters, NaniteUniformBuffer)
		SHADER_PARAMETER_STRUCT_INCLUDE(RaytracingTraversalStatistics::FShaderParameters, TraversalStatistics)

		SHADER_PARAMETER(uint32, VisualizationMode)
		SHADER_PARAMETER(float, TraversalBoxScale)
		SHADER_PARAMETER(float, TraversalClusterScale)
		SHADER_PARAMETER(float, TraversalTriangleScale)

		SHADER_PARAMETER(float, RTDebugVisualizationNaniteCutError)
	END_SHADER_PARAMETER_STRUCT()

	class FSupportProceduralPrimitive : SHADER_PERMUTATION_BOOL("ENABLE_TRACE_RAY_INLINE_PROCEDURAL_PRIMITIVE");
	class FPrintTraversalStatistics : SHADER_PERMUTATION_BOOL("PRINT_TRAVERSAL_STATISTICS");
	using FPermutationDomain = TShaderPermutationDomain<FSupportProceduralPrimitive, FPrintTraversalStatistics>;

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
		OutEnvironment.CompilerFlags.Add(CFLAG_InlineRayTracing);

		OutEnvironment.SetDefine(TEXT("INLINE_RAY_TRACING_THREAD_GROUP_SIZE_X"), ThreadGroupSizeX);
		OutEnvironment.SetDefine(TEXT("INLINE_RAY_TRACING_THREAD_GROUP_SIZE_Y"), ThreadGroupSizeY);
		OutEnvironment.SetDefine(TEXT("ENABLE_TRACE_RAY_INLINE_TRAVERSAL_STATISTICS"), 1);

		OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);
		OutEnvironment.SetDefine(TEXT("NANITE_USE_UNIFORM_BUFFER"), 1);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		bool bTraversalStats = PermutationVector.Get<FPrintTraversalStatistics>();
		bool bSupportsTraversalStats = FDataDrivenShaderPlatformInfo::GetSupportsRayTracingTraversalStatistics(Parameters.Platform);
		if (bTraversalStats && !bSupportsTraversalStats)
		{
			return false;
		}

		return IsRayTracingEnabledForProject(Parameters.Platform) && RHISupportsRayTracing(Parameters.Platform) && RHISupportsInlineRayTracing(Parameters.Platform);
	}

	static constexpr uint32 ThreadGroupSizeX = 8;
	static constexpr uint32 ThreadGroupSizeY = 4;
	static_assert(ThreadGroupSizeX*ThreadGroupSizeY == 32, "Current inline ray tracing implementation requires 1:1 mapping between thread groups and waves and only supports wave32 mode.");
};
IMPLEMENT_GLOBAL_SHADER(FRayTracingDebugTraversalCS, "/Engine/Private/RayTracing/RayTracingDebugTraversal.usf", "RayTracingDebugTraversalCS", SF_Compute);

void BindRayTracingDebugCHSMaterialBindings(FRHICommandList& RHICmdList, const FViewInfo& View, FRayTracingPipelineState* PipelineState)
{
	const int32 NumTotalBindings = View.VisibleRayTracingMeshCommands.Num();

	FSceneRenderingBulkObjectAllocator Allocator;

	auto Alloc = [&](uint32 Size, uint32 Align)
	{
		return RHICmdList.Bypass()
			? Allocator.Malloc(Size, Align)
			: RHICmdList.Alloc(Size, Align);
	};

	const uint32 MergedBindingsSize = sizeof(FRayTracingLocalShaderBindings) * NumTotalBindings;
	FRayTracingLocalShaderBindings* Bindings = (FRayTracingLocalShaderBindings*)Alloc(MergedBindingsSize, alignof(FRayTracingLocalShaderBindings));

	const uint32 NumUniformBuffers = 1;
	FRHIUniformBuffer** UniformBufferArray = (FRHIUniformBuffer**)Alloc(sizeof(FRHIUniformBuffer*) * NumUniformBuffers, alignof(FRHIUniformBuffer*));
	UniformBufferArray[0] = View.ViewUniformBuffer.GetReference();

	uint32 BindingIndex = 0;
	for (const FVisibleRayTracingMeshCommand VisibleMeshCommand : View.VisibleRayTracingMeshCommands)
	{
		const FRayTracingMeshCommand& MeshCommand = *VisibleMeshCommand.RayTracingMeshCommand;

		FRayTracingLocalShaderBindings Binding = {};
		Binding.InstanceIndex = VisibleMeshCommand.InstanceIndex;
		Binding.SegmentIndex = MeshCommand.GeometrySegmentIndex;
		Binding.UniformBuffers = UniformBufferArray;
		Binding.NumUniformBuffers = NumUniformBuffers;

		Bindings[BindingIndex] = Binding;
		BindingIndex++;
	}

	const bool bCopyDataToInlineStorage = false; // Storage is already allocated from RHICmdList, no extra copy necessary
	RHICmdList.SetRayTracingHitGroups(
		View.GetRayTracingSceneChecked(),
		PipelineState,
		NumTotalBindings, Bindings,
		bCopyDataToInlineStorage);
}

static bool RequiresRayTracingDebugCHS(uint32 DebugVisualizationMode)
{
	return DebugVisualizationMode == RAY_TRACING_DEBUG_VIZ_INSTANCES || DebugVisualizationMode == RAY_TRACING_DEBUG_VIZ_TRIANGLES;
}

static bool IsRayTracingDebugTraversalMode(uint32 DebugVisualizationMode)
{
	return DebugVisualizationMode == RAY_TRACING_DEBUG_VIZ_TRAVERSAL_NODE ||
		DebugVisualizationMode == RAY_TRACING_DEBUG_VIZ_TRAVERSAL_CLUSTER ||
		DebugVisualizationMode == RAY_TRACING_DEBUG_VIZ_TRAVERSAL_TRIANGLE ||
		DebugVisualizationMode == RAY_TRACING_DEBUG_VIZ_TRAVERSAL_ALL ||
		DebugVisualizationMode == RAY_TRACING_DEBUG_VIZ_TRAVERSAL_STATISTICS;
}

void FDeferredShadingSceneRenderer::PrepareRayTracingDebug(const FSceneViewFamily& ViewFamily, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	// Declare all RayGen shaders that require material closest hit shaders to be bound
	bool bEnabled = ViewFamily.EngineShowFlags.RayTracingDebug && ShouldRenderRayTracingEffect(ERayTracingPipelineCompatibilityFlags::FullPipeline);
	if (bEnabled)
	{
		auto RayGenShader = GetGlobalShaderMap(ViewFamily.GetShaderPlatform())->GetShader<FRayTracingDebugRGS>();
		OutRayGenShaders.Add(RayGenShader.GetRayTracingShader());
	}
}

void FDeferredShadingSceneRenderer::RenderRayTracingDebug(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef SceneColorTexture)
{
	static TMap<FName, uint32> RayTracingDebugVisualizationModes;
	if (RayTracingDebugVisualizationModes.Num() == 0)
	{
		RayTracingDebugVisualizationModes.Emplace(FName(*LOCTEXT("Radiance", "Radiance").ToString()),											RAY_TRACING_DEBUG_VIZ_RADIANCE);
		RayTracingDebugVisualizationModes.Emplace(FName(*LOCTEXT("World Normal", "World Normal").ToString()),									RAY_TRACING_DEBUG_VIZ_WORLD_NORMAL);
		RayTracingDebugVisualizationModes.Emplace(FName(*LOCTEXT("BaseColor", "BaseColor").ToString()),											RAY_TRACING_DEBUG_VIZ_BASE_COLOR);
		RayTracingDebugVisualizationModes.Emplace(FName(*LOCTEXT("DiffuseColor", "DiffuseColor").ToString()),									RAY_TRACING_DEBUG_VIZ_DIFFUSE_COLOR);
		RayTracingDebugVisualizationModes.Emplace(FName(*LOCTEXT("SpecularColor", "SpecularColor").ToString()),									RAY_TRACING_DEBUG_VIZ_SPECULAR_COLOR);
		RayTracingDebugVisualizationModes.Emplace(FName(*LOCTEXT("Opacity", "Opacity").ToString()),												RAY_TRACING_DEBUG_VIZ_OPACITY);
		RayTracingDebugVisualizationModes.Emplace(FName(*LOCTEXT("Metallic", "Metallic").ToString()),											RAY_TRACING_DEBUG_VIZ_METALLIC);
		RayTracingDebugVisualizationModes.Emplace(FName(*LOCTEXT("Specular", "Specular").ToString()),											RAY_TRACING_DEBUG_VIZ_SPECULAR);
		RayTracingDebugVisualizationModes.Emplace(FName(*LOCTEXT("Roughness", "Roughness").ToString()),											RAY_TRACING_DEBUG_VIZ_ROUGHNESS);
		RayTracingDebugVisualizationModes.Emplace(FName(*LOCTEXT("Ior", "Ior").ToString()),														RAY_TRACING_DEBUG_VIZ_IOR);
		RayTracingDebugVisualizationModes.Emplace(FName(*LOCTEXT("ShadingModelID", "ShadingModelID").ToString()),								RAY_TRACING_DEBUG_VIZ_SHADING_MODEL);
		RayTracingDebugVisualizationModes.Emplace(FName(*LOCTEXT("BlendingMode", "BlendingMode").ToString()),									RAY_TRACING_DEBUG_VIZ_BLENDING_MODE);
		RayTracingDebugVisualizationModes.Emplace(FName(*LOCTEXT("PrimitiveLightingChannelMask", "PrimitiveLightingChannelMask").ToString()),	RAY_TRACING_DEBUG_VIZ_LIGHTING_CHANNEL_MASK);
		RayTracingDebugVisualizationModes.Emplace(FName(*LOCTEXT("CustomData", "CustomData").ToString()),										RAY_TRACING_DEBUG_VIZ_CUSTOM_DATA);
		RayTracingDebugVisualizationModes.Emplace(FName(*LOCTEXT("GBufferAO", "GBufferAO").ToString()),											RAY_TRACING_DEBUG_VIZ_GBUFFER_AO);
		RayTracingDebugVisualizationModes.Emplace(FName(*LOCTEXT("IndirectIrradiance", "IndirectIrradiance").ToString()),						RAY_TRACING_DEBUG_VIZ_INDIRECT_IRRADIANCE);
		RayTracingDebugVisualizationModes.Emplace(FName(*LOCTEXT("World Position", "World Position").ToString()),								RAY_TRACING_DEBUG_VIZ_WORLD_POSITION);
		RayTracingDebugVisualizationModes.Emplace(FName(*LOCTEXT("HitKind", "HitKind").ToString()),												RAY_TRACING_DEBUG_VIZ_HITKIND);
		RayTracingDebugVisualizationModes.Emplace(FName(*LOCTEXT("Barycentrics", "Barycentrics").ToString()),									RAY_TRACING_DEBUG_VIZ_BARYCENTRICS);
		RayTracingDebugVisualizationModes.Emplace(FName(*LOCTEXT("PrimaryRays", "PrimaryRays").ToString()),										RAY_TRACING_DEBUG_VIZ_PRIMARY_RAYS);
		RayTracingDebugVisualizationModes.Emplace(FName(*LOCTEXT("World Tangent", "World Tangent").ToString()),									RAY_TRACING_DEBUG_VIZ_WORLD_TANGENT);
		RayTracingDebugVisualizationModes.Emplace(FName(*LOCTEXT("Anisotropy", "Anisotropy").ToString()),										RAY_TRACING_DEBUG_VIZ_ANISOTROPY);
		RayTracingDebugVisualizationModes.Emplace(FName(*LOCTEXT("Instances", "Instances").ToString()),											RAY_TRACING_DEBUG_VIZ_INSTANCES);
		RayTracingDebugVisualizationModes.Emplace(FName(*LOCTEXT("Performance", "Performance").ToString()),										RAY_TRACING_DEBUG_VIZ_PERFORMANCE);
		RayTracingDebugVisualizationModes.Emplace(FName(*LOCTEXT("Triangles", "Triangles").ToString()),											RAY_TRACING_DEBUG_VIZ_TRIANGLES);
		RayTracingDebugVisualizationModes.Emplace(FName(*LOCTEXT("FarField", "FarField").ToString()),											RAY_TRACING_DEBUG_VIZ_FAR_FIELD);
		RayTracingDebugVisualizationModes.Emplace(FName(*LOCTEXT("Traversal Node", "Traversal Node").ToString()),								RAY_TRACING_DEBUG_VIZ_TRAVERSAL_NODE);
		RayTracingDebugVisualizationModes.Emplace(FName(*LOCTEXT("Traversal Cluster", "Traversal Cluster").ToString()),							RAY_TRACING_DEBUG_VIZ_TRAVERSAL_CLUSTER);
		RayTracingDebugVisualizationModes.Emplace(FName(*LOCTEXT("Traversal Triangle", "Traversal Triangle").ToString()),						RAY_TRACING_DEBUG_VIZ_TRAVERSAL_TRIANGLE);
		RayTracingDebugVisualizationModes.Emplace(FName(*LOCTEXT("Traversal All", "Traversal All").ToString()),									RAY_TRACING_DEBUG_VIZ_TRAVERSAL_ALL);
		RayTracingDebugVisualizationModes.Emplace(FName(*LOCTEXT("Traversal Statistics", "Traversal Statistics").ToString()),					RAY_TRACING_DEBUG_VIZ_TRAVERSAL_STATISTICS);
	}

	uint32 DebugVisualizationMode;
	
	FString ConsoleViewMode = CVarRayTracingDebugMode.GetValueOnRenderThread();

	if (!ConsoleViewMode.IsEmpty())
	{
		DebugVisualizationMode = RayTracingDebugVisualizationModes.FindRef(FName(*ConsoleViewMode));
	}
	else if(View.CurrentRayTracingDebugVisualizationMode != NAME_None)
	{
		DebugVisualizationMode = RayTracingDebugVisualizationModes.FindRef(View.CurrentRayTracingDebugVisualizationMode);
	}
	else
	{
		// Set useful default value
		DebugVisualizationMode = RAY_TRACING_DEBUG_VIZ_BASE_COLOR;
	}

	if (DebugVisualizationMode == RAY_TRACING_DEBUG_VIZ_BARYCENTRICS)
	{
		return RenderRayTracingBarycentrics(GraphBuilder, View, SceneColorTexture, (bool)GVisualizeProceduralPrimitives);
	}

	if (IsRayTracingDebugTraversalMode(DebugVisualizationMode) && ShouldRenderRayTracingEffect(ERayTracingPipelineCompatibilityFlags::Inline))
	{
		const bool bPrintTraversalStats = FDataDrivenShaderPlatformInfo::GetSupportsRayTracingTraversalStatistics(GMaxRHIShaderPlatform)
			&& (DebugVisualizationMode == RAY_TRACING_DEBUG_VIZ_TRAVERSAL_STATISTICS);

		FRayTracingDebugTraversalCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRayTracingDebugTraversalCS::FParameters>();
		PassParameters->Output = GraphBuilder.CreateUAV(SceneColorTexture);
		PassParameters->TLAS = Scene->RayTracingScene.GetLayerSRVChecked(ERayTracingSceneLayer::Base);
		PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
		PassParameters->NaniteUniformBuffer = CreateDebugNaniteUniformBuffer(GraphBuilder, Scene->GPUScene.InstanceSceneDataSOAStride);

		PassParameters->VisualizationMode = DebugVisualizationMode;
		PassParameters->TraversalBoxScale = CVarRayTracingDebugTraversalBoxScale.GetValueOnAnyThread();
		PassParameters->TraversalClusterScale = CVarRayTracingDebugTraversalClusterScale.GetValueOnAnyThread();
		PassParameters->TraversalTriangleScale = CVarRayTracingDebugTraversalTriangleScale.GetValueOnAnyThread();

		PassParameters->RTDebugVisualizationNaniteCutError = 0.0f;

		RaytracingTraversalStatistics::FTraceRayInlineStatisticsData TraversalData;
		if (bPrintTraversalStats)
		{
			RaytracingTraversalStatistics::Init(GraphBuilder, TraversalData);
			RaytracingTraversalStatistics::SetParameters(GraphBuilder, TraversalData, PassParameters->TraversalStatistics);
		}

		FIntRect ViewRect = View.ViewRect;

		RDG_GPU_STAT_SCOPE(GraphBuilder, RayTracingDebug);

		const FIntPoint GroupSize(FRayTracingDebugTraversalCS::ThreadGroupSizeX, FRayTracingDebugTraversalCS::ThreadGroupSizeY);
		const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(ViewRect.Size(), GroupSize);

		FRayTracingDebugTraversalCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FRayTracingDebugTraversalCS::FSupportProceduralPrimitive>((bool)GVisualizeProceduralPrimitives);
		PermutationVector.Set<FRayTracingDebugTraversalCS::FPrintTraversalStatistics>(bPrintTraversalStats);
		
		TShaderRef<FRayTracingDebugTraversalCS> ComputeShader = View.ShaderMap->GetShader<FRayTracingDebugTraversalCS>(PermutationVector);

		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("RayTracingDebug"), ComputeShader, PassParameters, GroupCount);

		if (bPrintTraversalStats)
		{
			RaytracingTraversalStatistics::AddPrintPass(GraphBuilder, View, TraversalData);
		}

		return;
	}

	// Debug modes other than barycentrics and traversal require pipeline support.
	const bool bRayTracingPipeline = ShouldRenderRayTracingEffect(ERayTracingPipelineCompatibilityFlags::FullPipeline);
	if (!bRayTracingPipeline)
	{
		return;
	}

	if (DebugVisualizationMode == RAY_TRACING_DEBUG_VIZ_PRIMARY_RAYS) 
	{
		FRDGTextureRef OutputColor = nullptr;
		FRDGTextureRef HitDistanceTexture = nullptr;

		RenderRayTracingPrimaryRaysView(
				GraphBuilder, View, &OutputColor, &HitDistanceTexture, 1, 1, 1,
			ERayTracingPrimaryRaysFlag::PrimaryView);

		AddDrawTexturePass(GraphBuilder, View, OutputColor, SceneColorTexture, View.ViewRect.Min, View.ViewRect.Min, View.ViewRect.Size());
		return;
	}

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);
	auto RayGenShader = ShaderMap->GetShader<FRayTracingDebugRGS>();

	FRayTracingPipelineState* Pipeline = View.RayTracingMaterialPipeline;
	bool bRequiresBindings = false;

	if (RequiresRayTracingDebugCHS(DebugVisualizationMode))
	{
		FRayTracingPipelineStateInitializer Initializer;

		FRHIRayTracingShader* RayGenShaderTable[] = { RayGenShader.GetRayTracingShader() };
		Initializer.SetRayGenShaderTable(RayGenShaderTable);

		auto ClosestHitShader = ShaderMap->GetShader<FRayTracingDebugCHS>();
		FRHIRayTracingShader* HitGroupTable[] = { ClosestHitShader.GetRayTracingShader() };
		Initializer.SetHitGroupTable(HitGroupTable);
		Initializer.bAllowHitGroupIndexing = true; // Required for stable output using GetBaseInstanceIndex().
		Initializer.MaxPayloadSizeInBytes = RAY_TRACING_MAX_ALLOWED_PAYLOAD_SIZE;
		// TODO(UE-157946): This pipeline does not bind any miss shader and relies on the pipeline to do this automatically. This should be made explicit.
		Pipeline = PipelineStateCache::GetAndOrCreateRayTracingPipelineState(GraphBuilder.RHICmdList, Initializer);
		bRequiresBindings = true;
	}

	FRayTracingDebugRGS::FParameters* RayGenParameters = GraphBuilder.AllocParameters<FRayTracingDebugRGS::FParameters>();

	RayGenParameters->VisualizationMode = DebugVisualizationMode;
	RayGenParameters->ShouldUsePreExposure = View.Family->EngineShowFlags.Tonemapper;
	RayGenParameters->TimingScale = CVarRayTracingDebugTimingScale.GetValueOnAnyThread() / 25000.0f;
	RayGenParameters->OpaqueOnly = CVarRayTracingDebugModeOpaqueOnly.GetValueOnRenderThread();
	
	if (Lumen::UseFarField(ViewFamily))
	{
		RayGenParameters->MaxTraceDistance = Lumen::GetMaxTraceDistance(View);
		RayGenParameters->FarFieldMaxTraceDistance = Lumen::GetFarFieldMaxTraceDistance();
		RayGenParameters->FarFieldReferencePos = (FVector3f)Lumen::GetFarFieldReferencePos();	// LWC_TODO: Precision Loss
	}
	else
	{
		RayGenParameters->MaxTraceDistance = 0.0f;
		RayGenParameters->FarFieldMaxTraceDistance = 0.0f;
		RayGenParameters->FarFieldReferencePos = FVector3f(0.0f);
	}
	
	RayGenParameters->TLAS = Scene->RayTracingScene.GetLayerSRVChecked(ERayTracingSceneLayer::Base);
	RayGenParameters->ViewUniformBuffer = View.ViewUniformBuffer;
	RayGenParameters->Output = GraphBuilder.CreateUAV(SceneColorTexture);

	FIntRect ViewRect = View.ViewRect;

	RDG_GPU_STAT_SCOPE(GraphBuilder, RayTracingDebug);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("RayTracingDebug"),
		RayGenParameters,
		ERDGPassFlags::Compute,
		[this, RayGenParameters, RayGenShader, &View, Pipeline, ViewRect, bRequiresBindings](FRHIRayTracingCommandList& RHICmdList)
	{
		FRayTracingShaderBindingsWriter GlobalResources;
		SetShaderParameters(GlobalResources, RayGenShader, *RayGenParameters);

		if (bRequiresBindings)
		{
			BindRayTracingDebugCHSMaterialBindings(RHICmdList, View, Pipeline);
			RHICmdList.SetRayTracingMissShader(View.GetRayTracingSceneChecked(), 0, Pipeline, 0 /* ShaderIndexInPipeline */, 0, nullptr, 0);
		}

		RHICmdList.RayTraceDispatch(Pipeline, RayGenShader.GetRayTracingShader(), View.GetRayTracingSceneChecked(), GlobalResources, ViewRect.Size().X, ViewRect.Size().Y);
	});
}

#undef LOCTEXT_NAMESPACE
#endif
