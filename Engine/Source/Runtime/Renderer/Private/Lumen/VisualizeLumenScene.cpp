// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	VisualizeLumenScene.cpp
=============================================================================*/

#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "ReflectionEnvironment.h"
#include "LumenSceneUtils.h"
#include "LumenCubeMapTree.h"
#include "LumenRadianceCache.h"
#include "DynamicMeshBuilder.h"
#include "ShaderPrintParameters.h"

int32 GVisualizeLumenSceneGridPixelSize = 32;
FAutoConsoleVariableRef CVarVisualizeLumenSceneGridPixelSize(
	TEXT("r.Lumen.Visualize.GridPixelSize"),
	GVisualizeLumenSceneGridPixelSize,
	TEXT(""),
	ECVF_RenderThreadSafe
);

int32 GLumenVisualizeVoxels = 0;
FAutoConsoleVariableRef CVarLumenVisualizeVoxels(
	TEXT("r.Lumen.Visualize.Voxels"),
	GLumenVisualizeVoxels,
	TEXT("Visualize Lumen voxel Representation."),
	ECVF_RenderThreadSafe
);

int32 GLumenVisualizeStats = 0;
FAutoConsoleVariableRef CVarLumenVisualizeStats(
	TEXT("r.Lumen.Visualize.Stats"),
	GLumenVisualizeStats,
	TEXT("Print out Lumen scene stats."),
	ECVF_RenderThreadSafe
);

int32 GVisualizeLumenSceneTraceCards = 1;
FAutoConsoleVariableRef CVarVisualizeLumenSceneTraceCards(
	TEXT("r.Lumen.Visualize.TraceCards"),
	GVisualizeLumenSceneTraceCards,
	TEXT("Whether to use card tracing for lumen scene visualization."),
	ECVF_RenderThreadSafe
);

float GVisualizeLumenSceneMaxCardTraceDistance = -1.0f;
FAutoConsoleVariableRef CVarVisualizeLumenSceneCardMaxTraceDistance(
	TEXT("r.Lumen.Visualize.MaxCardTraceDistance"),
	GVisualizeLumenSceneMaxCardTraceDistance,
	TEXT("Max trace distance for Lumen scene visualization rays. Values below 0 will automatically derrive this from cone angle."),
	ECVF_RenderThreadSafe
);

int32 GVisualizeLumenSceneTraceRadianceCache = 0;
FAutoConsoleVariableRef CVarVisualizeLumenSceneTraceRadianceCache(
	TEXT("r.Lumen.Visualize.TraceRadianceCache"),
	GVisualizeLumenSceneTraceRadianceCache,
	TEXT("Whether to use radiance cache for Lumen scene visualization."),
	ECVF_RenderThreadSafe
);

float GVisualizeLumenSceneConeAngle = 1.0f;
FAutoConsoleVariableRef CVarVisualizeLumenSceneConeAngle(
	TEXT("r.Lumen.Visualize.ConeAngle"),
	GVisualizeLumenSceneConeAngle,
	TEXT("Visualize cone angle, in degrees."),
	ECVF_RenderThreadSafe
	);

float GVisualizeLumenSceneConeStepFactor = 2.0f;
FAutoConsoleVariableRef CVarVisualizeLumenSceneConeStepFactor(
	TEXT("r.Lumen.Visualize.ConeStepFactor"),
	GVisualizeLumenSceneConeStepFactor,
	TEXT("Cone step scale on sphere radius step size."),
	ECVF_RenderThreadSafe
	);

float GVisualizeLumenSceneVoxelStepFactor = 1.0f;
FAutoConsoleVariableRef CVarVisualizeLumenSceneVoxelStepFactor(
	TEXT("r.Lumen.Visualize.VoxelStepFactor"),
	GVisualizeLumenSceneVoxelStepFactor,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

float GVisualizeLumenSceneMinTraceDistance = 0;
FAutoConsoleVariableRef CVarVisualizeLumenSceneMinTraceDistance(
	TEXT("r.Lumen.Visualize.MinTraceDistance"),
	GVisualizeLumenSceneMinTraceDistance,
	TEXT(""),
	ECVF_RenderThreadSafe
);

float GVisualizeLumenSceneMaxTraceDistance = 100000;
FAutoConsoleVariableRef CVarVisualizeLumenSceneMaxTraceDistance(
	TEXT("r.Lumen.Visualize.MaxTraceDistance"),
	GVisualizeLumenSceneMaxTraceDistance,
	TEXT(""),
	ECVF_RenderThreadSafe
);

int32 GVisualizeLumenSceneClipmapIndex = -1;
FAutoConsoleVariableRef CVarVisualizeLumenSceneClipmapIndex(
	TEXT("r.Lumen.Visualize.ClipmapIndex"),
	GVisualizeLumenSceneClipmapIndex,
	TEXT("Which clipmap to use for the Lumen scene visualization. -1 uses all possible clipmaps."),
	ECVF_RenderThreadSafe
);

int32 GVisualizeLumenSceneVoxelFaceIndex = -1;
FAutoConsoleVariableRef CVarVisualizeLumenSceneVoxelFaceIndex(
	TEXT("r.Lumen.Visualize.VoxelFaceIndex"),
	GVisualizeLumenSceneVoxelFaceIndex,
	TEXT("Which voxel face to use for the Lumen scene visualization -X,+X,-Y,+Y,-Z,+Z. -1 uses all voxel faces."),
	ECVF_RenderThreadSafe
);

int32 GVisualizeLumenCardPlacement = 0;
FAutoConsoleVariableRef CVarVisualizeLumenSceneCardPlacement(
	TEXT("r.Lumen.Visualize.CardPlacement"),
	GVisualizeLumenCardPlacement,
	TEXT(""),
	ECVF_RenderThreadSafe
);

float GVisualizeLumenCardPlacementDistance = 5000.0f;
FAutoConsoleVariableRef CVarVisualizeLumenSceneCardPlacementDistance(
	TEXT("r.Lumen.Visualize.CardPlacementDistance"),
	GVisualizeLumenCardPlacementDistance,
	TEXT(""),
	ECVF_RenderThreadSafe
);

int32 GVisualizeLumenCardPlacementShowIndex = -1;
FAutoConsoleVariableRef CVarVisualizeLumenSceneCardPlacementShowIndex(
	TEXT("r.Lumen.Visualize.CardPlacementShowIndex"),
	GVisualizeLumenCardPlacementShowIndex,
	TEXT("Show only a single card per mesh."),
	ECVF_RenderThreadSafe
);

int32 GLumenSceneDumpStats = 0;
FAutoConsoleVariableRef CVarLumenSceneDumpStats(
	TEXT("r.LumenScene.DumpStats"),
	GLumenSceneDumpStats,
	TEXT("Whether to log Lumen scene stats on the next frame."),
	ECVF_RenderThreadSafe
);

float GVisualizeLumenSceneCardInterpolateInfluenceRadius = 10.0f;
FAutoConsoleVariableRef CVarCardInterpolateInfluenceRadius(
	TEXT("r.Lumen.Visualize.CardInterpolateInfluenceRadius"),
	GVisualizeLumenSceneCardInterpolateInfluenceRadius,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

BEGIN_SHADER_PARAMETER_STRUCT(FVisualizeLumenSceneParameters, )
	SHADER_PARAMETER(FIntVector, VoxelLightingGridResolution)
	SHADER_PARAMETER(float, PreviewConeAngle)
	SHADER_PARAMETER(float, TanPreviewConeAngle)
	SHADER_PARAMETER(float, VisualizeStepFactor)
	SHADER_PARAMETER(float, VoxelStepFactor)
	SHADER_PARAMETER(float, MinTraceDistance)
	SHADER_PARAMETER(float, MaxTraceDistance)
	SHADER_PARAMETER(float, MaxCardTraceDistanceForVoxelTracing)
	SHADER_PARAMETER(float, MaxCardTraceDistance)
	SHADER_PARAMETER(float, CardInterpolateInfluenceRadius)
	SHADER_PARAMETER(int, VisualizeClipmapIndex)
	SHADER_PARAMETER(int, VisualizeVoxelFaceIndex)
END_SHADER_PARAMETER_STRUCT()

class FVisualizeLumenSceneCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVisualizeLumenSceneCS)
	SHADER_USE_PARAMETER_STRUCT(FVisualizeLumenSceneCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntRect, ViewDimensions)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenMeshSDFGridParameters, MeshSDFGridParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FVisualizeLumenSceneParameters, VisualizeParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheParameters, RadianceCacheParameters)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWSceneColor)
	END_SHADER_PARAMETER_STRUCT()

	class FTraceCards : SHADER_PERMUTATION_BOOL("TRACE_CARDS");
	class FRadianceCache : SHADER_PERMUTATION_BOOL("RADIANCE_CACHE");

	using FPermutationDomain = TShaderPermutationDomain<FTraceCards, FRadianceCache>;

public:
	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
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

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());

		// Workaround for an internal PC FXC compiler crash when compiling with disabled optimizations
		if (Parameters.Platform == SP_PCD3D_SM5)
		{
			OutEnvironment.CompilerFlags.Add(CFLAG_ForceOptimization);
		}
	}

	static int32 GetGroupSize()
	{
		return 8;
	}
};

IMPLEMENT_GLOBAL_SHADER(FVisualizeLumenSceneCS, "/Engine/Private/Lumen/VisualizeLumenScene.usf", "VisualizeQuadsCS", SF_Compute);

class FVisualizeLumenSceneStatsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVisualizeLumenSceneStatsCS)
	SHADER_USE_PARAMETER_STRUCT(FVisualizeLumenSceneStatsCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters, ShaderPrintUniformBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, GlobalDistanceFieldPageFreeListAllocatorBuffer)
		SHADER_PARAMETER(uint32, GlobalDistanceFieldMaxPageNum)
	END_SHADER_PARAMETER_STRUCT()

public:
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

IMPLEMENT_GLOBAL_SHADER(FVisualizeLumenSceneStatsCS, "/Engine/Private/Lumen/VisualizeLumenScene.usf", "VisualizeStatsCS", SF_Compute);

class FVisualizeLumenVoxelsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVisualizeLumenVoxelsCS)
	SHADER_USE_PARAMETER_STRUCT(FVisualizeLumenVoxelsCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntRect, ViewDimensions)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FVisualizeLumenSceneParameters, VisualizeParameters)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWSceneColor)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}

	static int32 GetGroupSize()
	{
		return 8;
	}
};

IMPLEMENT_GLOBAL_SHADER(FVisualizeLumenVoxelsCS, "/Engine/Private/Lumen/VisualizeLumenScene.usf", "VisualizeLumenVoxelsCS", SF_Compute);

void FDeferredShadingSceneRenderer::RenderLumenSceneVisualization(FRDGBuilder& GraphBuilder)
{
	const FViewInfo& View = Views[0];

	if (Lumen::ShouldRenderLumenCardsForView(Scene, View))
	{
		RDG_EVENT_SCOPE(GraphBuilder, "VisualizeLumenScene");

		const bool bVisualizeScene = ViewFamily.EngineShowFlags.VisualizeLumenScene;
		const bool bVisualizeVoxels = GLumenVisualizeVoxels != 0;

		FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get();
		FRDGTextureRef SceneColor = GraphBuilder.RegisterExternalTexture(SceneContext.GetSceneColor());
		FRDGTextureUAVRef SceneColorUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(SceneColor));

		TRefCountPtr<IPooledRenderTarget> ResultSceneColor;
		ConvertToExternalTexture(GraphBuilder, SceneColor, ResultSceneColor);

		RenderScreenProbeGatherVisualizeTraces(GraphBuilder, View, SceneColor);

		FLumenCardTracingInputs TracingInputs(GraphBuilder, Scene, View);

		FVisualizeLumenSceneParameters VisualizeParameters;
		VisualizeParameters.VoxelLightingGridResolution = TracingInputs.VoxelGridResolution;
		VisualizeParameters.PreviewConeAngle = FMath::Clamp(GVisualizeLumenSceneConeAngle, 0.0f, 45.0f) * (float)PI / 180.0f;
		VisualizeParameters.TanPreviewConeAngle = FMath::Tan(VisualizeParameters.PreviewConeAngle);
		VisualizeParameters.VisualizeStepFactor = FMath::Clamp(GVisualizeLumenSceneConeStepFactor, .1f, 10.0f);
		VisualizeParameters.VoxelStepFactor = FMath::Clamp(GVisualizeLumenSceneVoxelStepFactor, .1f, 10.0f);
		VisualizeParameters.MinTraceDistance = FMath::Clamp(GVisualizeLumenSceneMinTraceDistance, .01f, 1000.0f);
		VisualizeParameters.MaxTraceDistance = FMath::Clamp(GVisualizeLumenSceneMaxTraceDistance, .01f, (float)HALF_WORLD_MAX);
		VisualizeParameters.VisualizeClipmapIndex = FMath::Clamp(GVisualizeLumenSceneClipmapIndex, -1, TracingInputs.NumClipmapLevels - 1);
		VisualizeParameters.VisualizeVoxelFaceIndex = FMath::Clamp(GVisualizeLumenSceneVoxelFaceIndex, -1, 5);
		VisualizeParameters.CardInterpolateInfluenceRadius = GVisualizeLumenSceneCardInterpolateInfluenceRadius;

		float MaxCardTraceDistance = GVisualizeLumenSceneMaxCardTraceDistance;

		if (MaxCardTraceDistance <= 0)
		{
			MaxCardTraceDistance = FMath::Clamp<float>(
				TracingInputs.ClipmapVoxelSizeAndRadius[0].W / FMath::Max(VisualizeParameters.TanPreviewConeAngle, 0.001f),
				VisualizeParameters.MinTraceDistance,
				VisualizeParameters.MaxTraceDistance);
		}

		VisualizeParameters.MaxCardTraceDistanceForVoxelTracing = FMath::Clamp(MaxCardTraceDistance, VisualizeParameters.MinTraceDistance, VisualizeParameters.MaxTraceDistance);
		VisualizeParameters.MaxCardTraceDistance = FMath::Clamp(MaxCardTraceDistance, VisualizeParameters.MinTraceDistance, VisualizeParameters.MaxTraceDistance);

		if (bVisualizeScene)
		{
			const uint32 CullGridPixelSize = FMath::Clamp(GVisualizeLumenSceneGridPixelSize, 8, 1024);
			const FIntPoint CullGridSizeXY = FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), CullGridPixelSize);
			const FIntVector CullGridSize = FIntVector(CullGridSizeXY.X, CullGridSizeXY.Y, 1);

			FLumenMeshSDFGridParameters MeshSDFGridParameters;			
			MeshSDFGridParameters.CardGridPixelSizeShift = FMath::FloorLog2(CullGridPixelSize);
			MeshSDFGridParameters.CullGridSize = CullGridSize;

			{
				const float CardTraceEndDistanceFromCamera = VisualizeParameters.MaxCardTraceDistance;

				CullMeshSDFObjectsToViewGrid(
					View,
					Scene,
					0,
					CardTraceEndDistanceFromCamera,
					CullGridPixelSize,
					1,
					FVector::ZeroVector,
					GraphBuilder,
					MeshSDFGridParameters);
			}
			
			if (Lumen::ShouldVisualizeHardwareRayTracing())
			{
				FLumenIndirectTracingParameters IndirectTracingParameters;
				IndirectTracingParameters.CardInterpolateInfluenceRadius = VisualizeParameters.CardInterpolateInfluenceRadius;
				IndirectTracingParameters.MinTraceDistance = VisualizeParameters.MinTraceDistance;
				IndirectTracingParameters.MaxTraceDistance = VisualizeParameters.MaxTraceDistance;
				IndirectTracingParameters.MaxCardTraceDistance = VisualizeParameters.MaxCardTraceDistance;
				LumenRadianceCache::FRadianceCacheParameters RadianceCacheParameters;
				LumenRadianceCache::GetParameters(View, GraphBuilder, RadianceCacheParameters);
				
				VisualizeHardwareRayTracing(
					GraphBuilder,
					Scene,
					GetSceneTextureParameters(GraphBuilder),
					View,
					TracingInputs,
					MeshSDFGridParameters,
					IndirectTracingParameters,
					RadianceCacheParameters,
					SceneColor);
			}
			else
			{
				FVisualizeLumenSceneCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVisualizeLumenSceneCS::FParameters>();
				PassParameters->ViewDimensions = View.ViewRect;
				PassParameters->RWSceneColor = SceneColorUAV;
				PassParameters->SceneTexturesStruct = CreateSceneTextureUniformBuffer(GraphBuilder, View.FeatureLevel);
				PassParameters->MeshSDFGridParameters = MeshSDFGridParameters;
				PassParameters->VisualizeParameters = VisualizeParameters;
				LumenRadianceCache::GetParameters(View, GraphBuilder, PassParameters->RadianceCacheParameters);
				GetLumenCardTracingParameters(View, TracingInputs, PassParameters->TracingParameters);

				FVisualizeLumenSceneCS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FVisualizeLumenSceneCS::FTraceCards>(GVisualizeLumenSceneTraceCards != 0);
				PermutationVector.Set<FVisualizeLumenSceneCS::FRadianceCache>(GVisualizeLumenSceneTraceRadianceCache != 0 && LumenRadianceCache::IsEnabled(View));
				PermutationVector = FVisualizeLumenSceneCS::RemapPermutation(PermutationVector);

				auto ComputeShader = View.ShaderMap->GetShader<FVisualizeLumenSceneCS>(PermutationVector);
				FIntPoint GroupSize(FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), FVisualizeLumenSceneCS::GetGroupSize()));

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("LumenSceneVisualization"),
					ComputeShader,
					PassParameters,
					FIntVector(GroupSize.X, GroupSize.Y, 1));
			}
		}
		else if (bVisualizeVoxels)
		{
			FVisualizeLumenVoxelsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVisualizeLumenVoxelsCS::FParameters>();
			PassParameters->ViewDimensions = View.ViewRect;
			PassParameters->RWSceneColor = SceneColorUAV;
			PassParameters->SceneTexturesStruct = CreateSceneTextureUniformBuffer(GraphBuilder, View.FeatureLevel);
			PassParameters->VisualizeParameters = VisualizeParameters;
			GetLumenCardTracingParameters(View, TracingInputs, PassParameters->TracingParameters);

			auto ComputeShader = View.ShaderMap->GetShader< FVisualizeLumenVoxelsCS >();
			FIntPoint GroupSize(FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), FVisualizeLumenVoxelsCS::GetGroupSize()));

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("LumenVoxelsVisualization"),
				ComputeShader,
				PassParameters,
				FIntVector(GroupSize.X, GroupSize.Y, 1));
		}

		check(ResultSceneColor.GetReference() == SceneContext.GetSceneColor().GetReference());
	}

	if (GAllowLumenScene
		&& DoesPlatformSupportLumenGI(ShaderPlatform)
		&& Views.Num() == 1
		&& GLumenVisualizeStats != 0
		&& View.GlobalDistanceFieldInfo.PageFreeListAllocatorBuffer)
	{
		FRDGBufferRef GlobalDistanceFieldPageFreeListAllocatorBuffer = GraphBuilder.RegisterExternalBuffer(View.GlobalDistanceFieldInfo.PageFreeListAllocatorBuffer, TEXT("PageFreeListAllocator"));

		FVisualizeLumenSceneStatsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVisualizeLumenSceneStatsCS::FParameters>();
		ShaderPrint::SetParameters(GraphBuilder, View, PassParameters->ShaderPrintUniformBuffer);
		PassParameters->GlobalDistanceFieldPageFreeListAllocatorBuffer = GraphBuilder.CreateSRV(GlobalDistanceFieldPageFreeListAllocatorBuffer, PF_R32_UINT);
		PassParameters->GlobalDistanceFieldMaxPageNum = View.GlobalDistanceFieldInfo.ParameterData.MaxPageNum;

		auto ComputeShader = View.ShaderMap->GetShader<FVisualizeLumenSceneStatsCS>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("LumenSceneStats"),
			ComputeShader,
			PassParameters,
			FIntVector(1, 1, 1));
	}

	RenderLumenRadianceCacheVisualization(GraphBuilder);
}

void AddBoxFaceTriangles(FDynamicMeshBuilder& MeshBuilder, int32 FaceIndex)
{
	const int32 BoxIndices[6][4] =
	{
		{ 0, 2, 3, 1 },	// back, -z
		{ 4, 5, 7, 6 },	// front, +z
		{ 0, 4, 6, 2 },	// left, -x
		{ 1, 3, 7, 5 },	// right, +x,
		{ 0, 4, 5, 1 },	// bottom, -y
		{ 2, 3, 7, 6 }	// top, +y
	};

	MeshBuilder.AddTriangle(BoxIndices[FaceIndex][0], BoxIndices[FaceIndex][1], BoxIndices[FaceIndex][2]);
	MeshBuilder.AddTriangle(BoxIndices[FaceIndex][0], BoxIndices[FaceIndex][2], BoxIndices[FaceIndex][3]);
}

void FDeferredShadingSceneRenderer::LumenScenePDIVisualization()
{
	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;
	const FDistanceFieldSceneData& DistanceFieldSceneData = Scene->DistanceFieldSceneData;

	if (GLumenSceneDumpStats)
	{
		int32 NumCards = 0;
		int32 NumVisibleCards = 0;
		int32 NumVisibleTexels = 0;

		for (const FCardSourceData& Card : LumenSceneData.Cards)
		{
			++NumCards;

			if (Card.bVisible)
			{
				++NumVisibleCards;
				NumVisibleTexels += Card.GetNumTexels();
			}
		}

		int32 NumCubeMapTrees = 0;
		int32 NumCubeMapTreeInstances = 0;
		int32 NumCubeMapTreeInstancesMerged = 0;
		for (const FLumenCubeMapTree& CubeMapTree : LumenSceneData.CubeMapTrees)
		{
			++NumCubeMapTrees;
			if (CubeMapTree.InstanceIndexOrMergedFlag > 0)
			{
				++NumCubeMapTreeInstances;
			}

			if (CubeMapTree.InstanceIndexOrMergedFlag == -1)
			{
				const TArray<FPrimitiveInstance>* PrimitiveInstances = CubeMapTree.PrimitiveSceneInfo->Proxy->GetPrimitiveInstances();
				if (PrimitiveInstances)
				{
					const int32 NumInstances = PrimitiveInstances->Num();
					NumCubeMapTreeInstancesMerged += NumInstances;
				}
			}
		}

		UE_LOG(LogRenderer, Log, TEXT("LumenScene Stats"));
		UE_LOG(LogRenderer, Log, TEXT("  Mesh SDF Objects: %d"), DistanceFieldSceneData.NumObjectsInBuffer);
		UE_LOG(LogRenderer, Log, TEXT("  Cube map trees: %d"), NumCubeMapTrees);
		UE_LOG(LogRenderer, Log, TEXT("  Cube map tree instances: %d"), NumCubeMapTreeInstances);
		UE_LOG(LogRenderer, Log, TEXT("  Cube map tree instances merged: %d"), NumCubeMapTreeInstancesMerged);
		UE_LOG(LogRenderer, Log, TEXT("  Cards: %d"), NumCards);
		UE_LOG(LogRenderer, Log, TEXT("  Visible cards: %d"), NumVisibleCards);
		UE_LOG(LogRenderer, Log, TEXT("  Visible cards texels: %.3fM"), NumVisibleTexels / (1024.0f * 1024.0f));
		UE_LOG(LogRenderer, Log, TEXT("  Cards left to capture: %d"), LumenSceneData.NumCardsLeftToCapture);
		UE_LOG(LogRenderer, Log, TEXT("  Cards left to reallocate: %d"), LumenSceneData.NumCardsLeftToReallocate);
		UE_LOG(LogRenderer, Log, TEXT("  Texels left to capture: %.3fM"), LumenSceneData.NumTexelsLeftToCapture / (1024.0f * 1024.0f));
		UE_LOG(LogRenderer, Log, TEXT("  Cards allocated memory: %.3fMb"), LumenSceneData.Cards.GetAllocatedSize() / (1024.0f * 1024.0f));
		UE_LOG(LogRenderer, Log, TEXT("  CubeMaps allocated memory: %.3fMb"), LumenSceneData.CubeMaps.GetAllocatedSize() / (1024.0f * 1024.0f));
		UE_LOG(LogRenderer, Log, TEXT("  CubeMapTrees allocated memory: %.3fMb"), LumenSceneData.CubeMapTrees.GetAllocatedSize() / (1024.0f * 1024.0f));

		GLumenSceneDumpStats = 0;
	}

	if (Lumen::ShouldRenderLumenCardsForView(Scene, Views[0]) && GVisualizeLumenCardPlacement != 0)
	{
		FViewElementPDI ViewPDI(&Views[0], nullptr, &Views[0].DynamicPrimitiveCollector);

		FConvexVolume ViewFrustum;
		GetViewFrustumBounds(ViewFrustum, Views[0].ViewMatrices.GetViewProjectionMatrix(), true);

		for (const FCardSourceData& Card : LumenSceneData.Cards)
		{
			bool bVisible = Card.bVisible;

			if (GVisualizeLumenCardPlacementShowIndex >= 0 && Card.FaceIndexInCubeMapTree != GVisualizeLumenCardPlacementShowIndex)
			{
				bVisible = false;
			}

			if (bVisible 
				&& Card.WorldBounds.ComputeSquaredDistanceToPoint(Views[0].ViewMatrices.GetViewOrigin()) < GVisualizeLumenCardPlacementDistance * GVisualizeLumenCardPlacementDistance
				&& ViewFrustum.IntersectBox(Card.WorldBounds.GetCenter(), Card.WorldBounds.GetExtent()))
			{
				uint32 CardHash = HashCombine(GetTypeHash(Card.Origin), GetTypeHash(Card.LocalExtent));
				CardHash = HashCombine(CardHash, GetTypeHash(Card.LocalToWorldRotationZ));

				const uint8 DepthPriority = SDPG_World;
				const uint8 CardHue = CardHash & 0xFF;
				const uint8 CardSaturation = 0xFF;
				const uint8 CardValue = 0xFF;

				FLinearColor CardColor = FLinearColor::MakeFromHSV8(CardHue, CardSaturation, CardValue);
				CardColor.A = 0.5f;

				FMatrix CardToWorld = FMatrix::Identity;
				CardToWorld.SetAxes(&Card.LocalToWorldRotationX, &Card.LocalToWorldRotationY, &Card.LocalToWorldRotationZ, &Card.Origin);

				const FBox LocalBounds(-Card.LocalExtent, Card.LocalExtent);

				DrawWireBox(&ViewPDI, CardToWorld, LocalBounds, CardColor, DepthPriority);

				FColoredMaterialRenderProxy* MaterialRenderProxy = new(FMemStack::Get()) FColoredMaterialRenderProxy(GEngine->EmissiveMeshMaterial->GetRenderProxy(), CardColor, NAME_Color);

				FDynamicMeshBuilder MeshBuilder(ViewPDI.View->GetFeatureLevel());

				for (int32 VertIndex = 0; VertIndex < 8; ++VertIndex)
				{
					FVector BoxVertex;
					BoxVertex.X = VertIndex & 0x1 ? LocalBounds.Max.X : LocalBounds.Min.X;
					BoxVertex.Y = VertIndex & 0x2 ? LocalBounds.Max.Y : LocalBounds.Min.Y;
					BoxVertex.Z = VertIndex & 0x4 ? LocalBounds.Max.Z : LocalBounds.Min.Z;
					MeshBuilder.AddVertex(BoxVertex, FVector2D(0, 0), FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), FColor::White);
				}

				AddBoxFaceTriangles(MeshBuilder, 1);

				MeshBuilder.Draw(&ViewPDI, CardToWorld, MaterialRenderProxy, DepthPriority, 0.0f);
			}
		}
	}

	static bool bVisualizeLumenSceneViewOrigin = false;

	if (bVisualizeLumenSceneViewOrigin)
	{
		const int32 NumClipmaps = GetNumLumenVoxelClipmaps();

		for (int32 ClipmapIndex = 0; ClipmapIndex < NumClipmaps; ClipmapIndex++)
		{
			FViewElementPDI ViewPDI(&Views[0], nullptr, &Views[0].DynamicPrimitiveCollector);
			const uint8 MarkerHue = (ClipmapIndex * 100) & 0xFF;
			const uint8 MarkerSaturation = 0xFF;
			const uint8 MarkerValue = 0xFF;

			FLinearColor MarkerColor = FLinearColor::MakeFromHSV8(MarkerHue, MarkerSaturation, MarkerValue);
			MarkerColor.A = 0.5f;
			const FVector LumenSceneCameraOrigin = GetLumenSceneViewOrigin(Views[0], ClipmapIndex);
			DrawWireSphere(&ViewPDI, LumenSceneCameraOrigin, MarkerColor, 10 * (1 << ClipmapIndex), 32, SDPG_World);
		}
	}

	LumenRadianceCachePDIVisualization();
}