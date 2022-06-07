// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteMaterials.h"
#include "NaniteDrawList.h"
#include "NaniteVisualizationData.h"
#include "Rendering/NaniteResources.h"
#include "Rendering/NaniteStreamingManager.h"
#include "RHI.h"
#include "SceneUtils.h"
#include "ScenePrivate.h"
#include "ScreenPass.h"
#include "GPUScene.h"
#include "ClearQuad.h"
#include "RendererModule.h"
#include "PixelShaderUtils.h"
#include "Lumen/LumenSceneRendering.h"
#include "Strata/Strata.h"

DEFINE_GPU_STAT(NaniteMaterials);
DEFINE_GPU_STAT(NaniteDepth);

BEGIN_SHADER_PARAMETER_STRUCT(FDummyDepthDecompressParameters, )
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SceneDepth)
END_SHADER_PARAMETER_STRUCT()

int32 GNaniteResummarizeHTile = 1;
static FAutoConsoleVariableRef CVarNaniteResummarizeHTile(
	TEXT("r.Nanite.ResummarizeHTile"),
	GNaniteResummarizeHTile,
	TEXT("")
);

static TAutoConsoleVariable<int32> CVarParallelBasePassBuild(
	TEXT("r.Nanite.ParallelBasePassBuild"),
	1,
	TEXT(""),
	ECVF_RenderThreadSafe
);

int32 GNaniteClassifyWithResolve = 1;
static FAutoConsoleVariableRef CVarNaniteClassifyWithResolve(
	TEXT("r.Nanite.ClassifyWithResolve"),
	GNaniteClassifyWithResolve,
	TEXT("")
);

float GRayTracingCutError = 0.0f;
static FAutoConsoleVariableRef CVarRayTracingCutError(
	TEXT("r.RayTracing.Nanite.CutError"),
	GRayTracingCutError,
	TEXT("Global target cut error to control quality when using procedural raytracing geometry for Nanite meshes.")
);

#if WITH_EDITORONLY_DATA
extern int32 GNaniteIsolateInvalidCoarseMesh;
#endif

class FNaniteMarkStencilPS : public FNaniteGlobalShader
{
	DECLARE_GLOBAL_SHADER(FNaniteMarkStencilPS);
	SHADER_USE_PARAMETER_STRUCT(FNaniteMarkStencilPS, FNaniteGlobalShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisBuffer64)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FNaniteMarkStencilPS, "/Engine/Private/Nanite/NaniteExportGBuffer.usf", "MarkStencilPS", SF_Pixel);

class FEmitMaterialDepthPS : public FNaniteGlobalShader
{
	DECLARE_GLOBAL_SHADER(FEmitMaterialDepthPS);
	SHADER_USE_PARAMETER_STRUCT(FEmitMaterialDepthPS, FNaniteGlobalShader);

	class FMaterialResolveDim : SHADER_PERMUTATION_BOOL("MATERIAL_RESOLVE");
	using FPermutationDomain = TShaderPermutationDomain<FMaterialResolveDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FNaniteGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(uint32, DummyZero)

		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, VisibleClustersSWHW)
		SHADER_PARAMETER(FIntVector4, PageConstants)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, ClusterPageData)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, MaterialResolve)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisBuffer64)

		SHADER_PARAMETER_SRV(ByteAddressBuffer, MaterialSlotTable)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, MaterialDepthTable)

		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FEmitMaterialDepthPS, "/Engine/Private/Nanite/NaniteExportGBuffer.usf", "EmitMaterialDepthPS", SF_Pixel);

IMPLEMENT_GLOBAL_SHADER(FNaniteIndirectMaterialVS, "/Engine/Private/Nanite/NaniteExportGBuffer.usf", "FullScreenVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FNaniteMultiViewMaterialVS, "/Engine/Private/Nanite/NaniteExportGBuffer.usf", "FullScreenVS", SF_Vertex);

class FEmitSceneDepthPS : public FNaniteGlobalShader
{
	DECLARE_GLOBAL_SHADER(FEmitSceneDepthPS);
	SHADER_USE_PARAMETER_STRUCT(FEmitSceneDepthPS, FNaniteGlobalShader);

	class FVelocityExportDim : SHADER_PERMUTATION_BOOL("VELOCITY_EXPORT");
	class FMaterialResolveDim : SHADER_PERMUTATION_BOOL("MATERIAL_RESOLVE");
	using FPermutationDomain = TShaderPermutationDomain<FVelocityExportDim, FMaterialResolveDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FNaniteGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FPackedView>, InViews)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, VisibleClustersSWHW)
		SHADER_PARAMETER(FIntVector4, PageConstants)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, ClusterPageData)
		SHADER_PARAMETER_RDG_TEXTURE( Texture2D<UlongType>,	VisBuffer64 )
		SHADER_PARAMETER_SRV(ByteAddressBuffer, MaterialSlotTable)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FEmitSceneDepthPS, "/Engine/Private/Nanite/NaniteExportGBuffer.usf", "EmitSceneDepthPS", SF_Pixel);

class FEmitSceneStencilPS : public FNaniteGlobalShader
{
	DECLARE_GLOBAL_SHADER(FEmitSceneStencilPS);
	SHADER_USE_PARAMETER_STRUCT(FEmitSceneStencilPS, FNaniteGlobalShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FNaniteGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, VisibleClustersSWHW)
		SHADER_PARAMETER(FIntVector4, PageConstants)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, ClusterPageData)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, MaterialResolve)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisBuffer64)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FEmitSceneStencilPS, "/Engine/Private/Nanite/NaniteExportGBuffer.usf", "EmitSceneStencilPS", SF_Pixel);

class FEmitSceneDepthStencilPS : public FNaniteGlobalShader
{
	DECLARE_GLOBAL_SHADER(FEmitSceneDepthStencilPS);
	SHADER_USE_PARAMETER_STRUCT(FEmitSceneDepthStencilPS, FNaniteGlobalShader);

	class FVelocityExportDim : SHADER_PERMUTATION_BOOL("VELOCITY_EXPORT");
	using FPermutationDomain = TShaderPermutationDomain<FVelocityExportDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FNaniteGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FPackedView>, InViews)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, VisibleClustersSWHW)
		SHADER_PARAMETER(FIntVector4, PageConstants)
		SHADER_PARAMETER(uint32, StencilClear)
		SHADER_PARAMETER(uint32, StencilDecal)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, ClusterPageData)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisBuffer64)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, MaterialSlotTable)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FEmitSceneDepthStencilPS, "/Engine/Private/Nanite/NaniteExportGBuffer.usf", "EmitSceneDepthStencilPS", SF_Pixel);

class FDepthExportCS : public FNaniteGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDepthExportCS);
	SHADER_USE_PARAMETER_STRUCT(FDepthExportCS, FNaniteGlobalShader);

	class FVelocityExportDim : SHADER_PERMUTATION_BOOL("VELOCITY_EXPORT");
	using FPermutationDomain = TShaderPermutationDomain<FVelocityExportDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FPackedView>, InViews)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, VisibleClustersSWHW)
		SHADER_PARAMETER(FIntVector4, PageConstants)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, ClusterPageData)
		SHADER_PARAMETER(FIntVector4, DepthExportConfig)
		SHADER_PARAMETER(FIntVector4, ViewRectMax)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisBuffer64)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, Velocity)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, MaterialResolve)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTextureMetadata, SceneHTile)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, SceneDepth)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, SceneStencil)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTextureMetadata, MaterialHTile)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, MaterialDepth)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, MaterialSlotTable)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, MaterialDepthTable)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FDepthExportCS, "/Engine/Private/Nanite/NaniteDepthExport.usf", "DepthExport", SF_Compute);

class FInitializeMaterialsCS : public FNaniteGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FInitializeMaterialsCS);
	SHADER_USE_PARAMETER_STRUCT(FInitializeMaterialsCS, FNaniteGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, MaterialTileCount)
		SHADER_PARAMETER(uint32, MaterialRemapCount)
		SHADER_PARAMETER(uint32, MaterialSlotCount)
		SHADER_PARAMETER(uint32, MaterialBinCount)
		SHADER_PARAMETER(uint32, TopologyIndexCount)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, MaterialIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, MaterialTileRemap)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FInitializeMaterialsCS, "/Engine/Private/Nanite/NaniteMaterialCulling.usf", "InitializeMaterials", SF_Compute);

class FClassifyMaterialsCS : public FNaniteGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FClassifyMaterialsCS);
	SHADER_USE_PARAMETER_STRUCT(FClassifyMaterialsCS, FNaniteGlobalShader);

	class FMaterialResolveDim : SHADER_PERMUTATION_BOOL("MATERIAL_RESOLVE");
	using FPermutationDomain = TShaderPermutationDomain<FMaterialResolveDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, VisibleClustersSWHW)
		SHADER_PARAMETER(FIntVector4, PageConstants)
		SHADER_PARAMETER(FIntVector4, ViewRect)
		SHADER_PARAMETER(FIntPoint, FetchClamp)
		SHADER_PARAMETER(uint32, MaterialTileCount)
		SHADER_PARAMETER(uint32, MaterialRemapCount)
		SHADER_PARAMETER(uint32, MaterialSlotCount)
		SHADER_PARAMETER(uint32, MaterialBinCount)
		SHADER_PARAMETER(uint32, RowTileCount)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, ClusterPageData)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisBuffer64)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, MaterialIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, MaterialTileRemap)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, MaterialSlotTable)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, MaterialDepthTable)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, MaterialResolve)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		// TODO: Reintroduce wave-ops
		// FPermutationDomain PermutationVector(Parameters.PermutationId);
		// FDataDrivenShaderPlatformInfo::GetSupportsWaveOperations(Parameters.Platform)

		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FNaniteGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		// TODO: Reintroduce wave-ops
		//FPermutationDomain PermutationVector(Parameters.PermutationId);
	}
};
IMPLEMENT_GLOBAL_SHADER(FClassifyMaterialsCS, "/Engine/Private/Nanite/NaniteMaterialCulling.usf", "ClassifyMaterials", SF_Compute);

BEGIN_SHADER_PARAMETER_STRUCT(FNaniteMarkStencilRectsParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FPixelShaderUtils::FRasterizeToRectsVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FNaniteMarkStencilPS::FParameters, PS)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FNaniteEmitMaterialIdRectsParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FPixelShaderUtils::FRasterizeToRectsVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FEmitMaterialDepthPS::FParameters, PS)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FNaniteEmitDepthRectsParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FPixelShaderUtils::FRasterizeToRectsVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FEmitSceneDepthPS::FParameters, PS)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FNaniteEmitGBufferParameters, )
	SHADER_PARAMETER(FIntVector4,	VisualizeConfig)
	SHADER_PARAMETER(FIntVector4,	PageConstants)
	SHADER_PARAMETER(uint32,		MaxVisibleClusters)
	SHADER_PARAMETER(uint32,		MaxNodes)
	SHADER_PARAMETER(uint32,		RenderFlags)
	SHADER_PARAMETER(float,			RayTracingCutError)
	SHADER_PARAMETER(uint32,		MaterialRemapCount)
	SHADER_PARAMETER(FIntPoint,		GridSize)

	SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, ClusterPageData)
	SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, HierarchyBuffer)

	SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, VisibleClustersSWHW)

	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisBuffer64)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, DbgBuffer64)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>,  DbgBuffer32)

	RDG_BUFFER_ACCESS(MaterialIndirectArgs, ERHIAccess::IndirectArgs)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, MaterialTileRemap)

	// Multi view
	SHADER_PARAMETER(uint32, MultiViewEnabled)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, MultiViewIndices)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, MultiViewRectScaleOffsets)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FPackedView>, InViews)

	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)	// To access VTFeedbackBuffer
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FOpaqueBasePassUniformParameters, BasePass)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardPassUniformParameters, CardPass)

	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

namespace Nanite
{

void DrawBasePass(
	FRDGBuilder& GraphBuilder,
	TArray<FNaniteMaterialPassCommand, SceneRenderingAllocator>& MaterialPassCommands,
	const FSceneRenderer& SceneRenderer,
	const FSceneTextures& SceneTextures,
	const FDBufferTextures& DBufferTextures,
	const FScene& Scene,
	const FViewInfo& View,
	const FRasterResults& RasterResults
)
{
	checkSlow(DoesPlatformSupportNanite(GMaxRHIShaderPlatform));

	LLM_SCOPE_BYTAG(Nanite);
	RDG_EVENT_SCOPE(GraphBuilder, "Nanite::BasePass");
	RDG_GPU_STAT_SCOPE(GraphBuilder, NaniteMaterials);

	const int32 ViewWidth		= View.ViewRect.Max.X - View.ViewRect.Min.X;
	const int32 ViewHeight		= View.ViewRect.Max.Y - View.ViewRect.Min.Y;
	const FIntPoint ViewSize	= FIntPoint(ViewWidth, ViewHeight);

	const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);

	TStaticArray<FTextureRenderTargetBinding, MaxSimultaneousRenderTargets> BasePassTextures;
	uint32 BasePassTextureCount = SceneTextures.GetGBufferRenderTargets(BasePassTextures);
	Strata::AppendStrataMRTs(SceneRenderer, BasePassTextureCount, BasePassTextures);
	TArrayView<FTextureRenderTargetBinding> BasePassTexturesView = MakeArrayView(BasePassTextures.GetData(), BasePassTextureCount);

	FRDGTextureRef MaterialDepth	= RasterResults.MaterialDepth ? RasterResults.MaterialDepth : SystemTextures.Black;
	FRDGTextureRef VisBuffer64		= RasterResults.VisBuffer64   ? RasterResults.VisBuffer64   : SystemTextures.Black;
	FRDGTextureRef DbgBuffer64		= RasterResults.DbgBuffer64   ? RasterResults.DbgBuffer64   : SystemTextures.Black;
	FRDGTextureRef DbgBuffer32		= RasterResults.DbgBuffer32   ? RasterResults.DbgBuffer32   : SystemTextures.Black;

	FRDGBufferRef VisibleClustersSWHW	= RasterResults.VisibleClustersSWHW;

	// TODO: Reintroduce wave-ops
	// FDataDrivenShaderPlatformInfo::GetSupportsWaveOperations(GMaxRHIShaderPlatform)

	const FIntPoint TileGridDim = FMath::DivideAndRoundUp(ViewSize, { 64, 64 });

	const uint32 MaxMaterialSlots = NANITE_MAX_STATE_BUCKET_ID + 1;

	FRDGBufferRef MaterialIndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDrawIndexedIndirectParameters>(MaxMaterialSlots), TEXT("Nanite.MaterialIndirectArgs"));

	FRDGBufferRef MultiViewIndices = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1), TEXT("Nanite.DummyMultiViewIndices"));
	FRDGBufferRef MultiViewRectScaleOffsets = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), 1), TEXT("Nanite.DummyMultiViewRectScaleOffsets"));
	FRDGBufferRef ViewsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), 1), TEXT("Nanite.PackedViews"));

	const uint32 HighestMaterialSlot = Scene.NaniteMaterials[ENaniteMeshPass::BasePass].GetHighestMaterialSlot();
	const uint32 HighestMaterialBin  = FMath::DivideAndRoundUp(HighestMaterialSlot, 32u);

	const FIntPoint	TileGridSize	= FMath::DivideAndRoundUp(View.ViewRect.Max - View.ViewRect.Min, { 64, 64 });
	const uint32	TileCount		= TileGridSize.X * TileGridSize.Y;
	const uint32	TileRemaps		= FMath::DivideAndRoundUp(TileCount, 32u);

	FRDGBufferRef MaterialTileRemap = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), TileRemaps * MaxMaterialSlots), TEXT("Nanite.MaterialTileRemap"));

	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(MultiViewIndices), 0);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(MultiViewRectScaleOffsets), 0);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(ViewsBuffer), 0);

	// Classify materials for tile culling
	// TODO: Run velocity export in here instead of depth pre-pass?
	{
		// Initialize acceleration/indexing structures for tile classification
		{
			auto ComputeShader = View.ShaderMap->GetShader<FInitializeMaterialsCS>();
			FInitializeMaterialsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FInitializeMaterialsCS::FParameters>();
			PassParameters->MaterialSlotCount		= HighestMaterialSlot;
			PassParameters->MaterialTileCount		= TileGridSize.X * TileGridSize.Y;
			PassParameters->MaterialRemapCount		= TileRemaps;
			PassParameters->TopologyIndexCount		= GRHISupportsRectTopology ? 3 : 6;
			PassParameters->MaterialIndirectArgs	= GraphBuilder.CreateUAV(FRDGBufferUAVDesc(MaterialIndirectArgs, PF_R32_UINT));
			PassParameters->MaterialTileRemap		= GraphBuilder.CreateUAV(MaterialTileRemap);
			PassParameters->MaterialBinCount		= HighestMaterialBin;

			const FIntVector DispatchDim = FComputeShaderUtils::GetGroupCount(PassParameters->MaterialSlotCount, 64);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Initialize Materials"),
				ComputeShader,
				PassParameters,
				DispatchDim
			);
		}

		// Material tile classification
		{
			FClassifyMaterialsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FClassifyMaterialsCS::FParameters>();
			PassParameters->View					= View.ViewUniformBuffer;
			PassParameters->VisibleClustersSWHW		= GraphBuilder.CreateSRV(VisibleClustersSWHW);
			PassParameters->PageConstants			= RasterResults.PageConstants;
			PassParameters->ClusterPageData			= Nanite::GStreamingManager.GetClusterPageDataSRV(GraphBuilder);
			PassParameters->VisBuffer64				= VisBuffer64;
			PassParameters->MaterialSlotTable		= Scene.NaniteMaterials[ENaniteMeshPass::BasePass].GetMaterialSlotSRV();
			PassParameters->MaterialDepthTable		= Scene.NaniteMaterials[ENaniteMeshPass::BasePass].GetMaterialDepthSRV();
			PassParameters->MaterialResolve			= RasterResults.MaterialResolve;
			PassParameters->MaterialIndirectArgs	= GraphBuilder.CreateUAV(FRDGBufferUAVDesc(MaterialIndirectArgs, PF_R32_UINT));
			PassParameters->MaterialTileRemap		= GraphBuilder.CreateUAV(MaterialTileRemap);
			PassParameters->MaterialSlotCount		= HighestMaterialSlot;
			PassParameters->MaterialTileCount		= TileGridSize.X * TileGridSize.Y;
			PassParameters->MaterialRemapCount		= TileRemaps;
			PassParameters->MaterialBinCount		= HighestMaterialBin;

			uint32 DispatchGroupSize = 0;

			PassParameters->ViewRect = FIntVector4(View.ViewRect.Min.X, View.ViewRect.Min.Y, View.ViewRect.Max.X, View.ViewRect.Max.Y);
			DispatchGroupSize = 64;
			PassParameters->FetchClamp = View.ViewRect.Max - 1;

			const FIntVector DispatchDim = FComputeShaderUtils::GetGroupCount(View.ViewRect.Max - View.ViewRect.Min, DispatchGroupSize);

			PassParameters->RowTileCount = DispatchDim.X;

			FClassifyMaterialsCS::FPermutationDomain PermutationMaterialResolveCS;
			PermutationMaterialResolveCS.Set<FClassifyMaterialsCS::FMaterialResolveDim>(GNaniteClassifyWithResolve != 0);
			auto ComputeShader = View.ShaderMap->GetShader<FClassifyMaterialsCS>(PermutationMaterialResolveCS);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Classify Materials"),
				ComputeShader,
				PassParameters,
				DispatchDim
			);
		}
	}

	// Emit GBuffer Values
	{
		FNaniteEmitGBufferParameters* PassParameters = GraphBuilder.AllocParameters<FNaniteEmitGBufferParameters>();

		PassParameters->PageConstants		= RasterResults.PageConstants;
		PassParameters->MaxVisibleClusters	= RasterResults.MaxVisibleClusters;
		PassParameters->MaxNodes			= RasterResults.MaxNodes;
		PassParameters->RenderFlags			= RasterResults.RenderFlags;
		PassParameters->RayTracingCutError	= GRayTracingCutError;
		PassParameters->MaterialRemapCount	= TileRemaps;
			
		PassParameters->ClusterPageData		= Nanite::GStreamingManager.GetClusterPageDataSRV(GraphBuilder);
		PassParameters->HierarchyBuffer		= Nanite::GStreamingManager.GetHierarchySRV(GraphBuilder);

		PassParameters->VisibleClustersSWHW = GraphBuilder.CreateSRV(VisibleClustersSWHW);

		PassParameters->MaterialTileRemap = GraphBuilder.CreateSRV(MaterialTileRemap, PF_R32_UINT);
		PassParameters->MaterialIndirectArgs = MaterialIndirectArgs;

		PassParameters->MultiViewEnabled = 0;
		PassParameters->MultiViewIndices = GraphBuilder.CreateSRV(MultiViewIndices);
		PassParameters->MultiViewRectScaleOffsets = GraphBuilder.CreateSRV(MultiViewRectScaleOffsets);
		PassParameters->InViews = GraphBuilder.CreateSRV(ViewsBuffer);

		PassParameters->VisBuffer64 = VisBuffer64;
		PassParameters->DbgBuffer64 = DbgBuffer64;
		PassParameters->DbgBuffer32 = DbgBuffer32;
		PassParameters->RenderTargets = GetRenderTargetBindings(ERenderTargetLoadAction::ELoad, BasePassTexturesView);

		PassParameters->View = View.ViewUniformBuffer; // To get VTFeedbackBuffer
		PassParameters->BasePass = CreateOpaqueBasePassUniformBuffer(GraphBuilder, View, 0, {}, DBufferTextures, nullptr);
		PassParameters->GridSize = FMath::DivideAndRoundUp(View.ViewRect.Max - View.ViewRect.Min, { 64, 64 });


		const FExclusiveDepthStencil MaterialDepthStencil = UseComputeDepthExport()
			? FExclusiveDepthStencil::DepthWrite_StencilNop
			: FExclusiveDepthStencil::DepthWrite_StencilWrite;

		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
			MaterialDepth,
			ERenderTargetLoadAction::ELoad,
			ERenderTargetLoadAction::ELoad,
			MaterialDepthStencil
		);

		TShaderMapRef<FNaniteIndirectMaterialVS> NaniteVertexShader(View.ShaderMap);
				
		ERDGPassFlags RDGPassFlags = ERDGPassFlags::Raster;

		// Skip render pass when parallel because that's taken care of by the FRDGParallelCommandListSet
		bool bParallelBasePassBuild = GRHICommandList.UseParallelAlgorithms() && CVarParallelBasePassBuild.GetValueOnRenderThread() != 0 && FParallelMeshDrawCommandPass::IsOnDemandShaderCreationEnabled();
		if (bParallelBasePassBuild)
		{
			RDGPassFlags |= ERDGPassFlags::SkipRenderPass;
		}

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("Emit GBuffer"),
			PassParameters,
			RDGPassFlags,
			[PassParameters, &SceneRenderer, &Scene, NaniteVertexShader, &View, &MaterialPassCommands, bParallelBasePassBuild](const FRDGPass* InPass, FRHICommandListImmediate& RHICmdListImmediate)
		{
			RHICmdListImmediate.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

			FNaniteUniformParameters UniformParams;
			UniformParams.PageConstants = PassParameters->PageConstants;
			UniformParams.MaxVisibleClusters= PassParameters->MaxVisibleClusters;
			UniformParams.MaxNodes = PassParameters->MaxNodes;
			UniformParams.RenderFlags = PassParameters->RenderFlags;
			UniformParams.RayTracingCutError = PassParameters->RayTracingCutError;

			UniformParams.MaterialConfig.X = 1; // Indirect
			UniformParams.MaterialConfig.Y = PassParameters->GridSize.X;
			UniformParams.MaterialConfig.Z = PassParameters->GridSize.Y;
			UniformParams.MaterialConfig.W = 0;

			FIntPoint ScaledSize = PassParameters->GridSize * 64;
			UniformParams.RectScaleOffset = FVector4f(
				float(ScaledSize.X) / float(View.ViewRect.Max.X - View.ViewRect.Min.X),
				float(ScaledSize.Y) / float(View.ViewRect.Max.Y - View.ViewRect.Min.Y),
				0.0f,
				0.0f
			);

			UniformParams.ClusterPageData = PassParameters->ClusterPageData->GetRHI();
			UniformParams.HierarchyBuffer = PassParameters->HierarchyBuffer->GetRHI();

			UniformParams.VisibleClustersSWHW = PassParameters->VisibleClustersSWHW->GetRHI();

			UniformParams.MaterialTileRemap = PassParameters->MaterialTileRemap->GetRHI();

			UniformParams.MultiViewEnabled = PassParameters->MultiViewEnabled;
			UniformParams.MultiViewIndices = PassParameters->MultiViewIndices->GetRHI();
			UniformParams.MultiViewRectScaleOffsets = PassParameters->MultiViewRectScaleOffsets->GetRHI();
			UniformParams.InViews = PassParameters->InViews->GetRHI();

			UniformParams.VisBuffer64 = PassParameters->VisBuffer64->GetRHI();
			UniformParams.DbgBuffer64 = PassParameters->DbgBuffer64->GetRHI();
			UniformParams.DbgBuffer32 = PassParameters->DbgBuffer32->GetRHI();
			const_cast<FScene&>(Scene).UniformBuffers.NaniteUniformBuffer.UpdateUniformBufferImmediate(UniformParams);

			const uint32 TileCount = UniformParams.MaterialConfig.Y * UniformParams.MaterialConfig.Z; // (W * H)

			PassParameters->MaterialIndirectArgs->MarkResourceAsUsed();

			DrawNaniteMaterialPasses(
				InPass,
				SceneRenderer,
				Scene,
				View,
				TileCount,
				bParallelBasePassBuild,
				FParallelCommandListBindings(PassParameters),
				NaniteVertexShader,
				RHICmdListImmediate,
				PassParameters->MaterialIndirectArgs->GetIndirectRHICallBuffer(),
				MaterialPassCommands
			);
		});
	}

	ExtractShadingStats(GraphBuilder, View, MaterialIndirectArgs, HighestMaterialSlot);
}

void EmitDepthTargets(
	FRDGBuilder& GraphBuilder,
	const FScene& Scene,
	const FViewInfo& View,
	const FIntVector4& PageConstants,
	FRDGBufferRef VisibleClustersSWHW,
	FRDGBufferRef ViewsBuffer,
	FRDGTextureRef SceneDepth,
	FRDGTextureRef VisBuffer64,
	FRDGTextureRef VelocityBuffer,
	FRDGTextureRef& OutMaterialDepth,
	FRDGTextureRef& OutMaterialResolve,
	bool bPrePass,
	bool bStencilMask
)
{
	LLM_SCOPE_BYTAG(Nanite);
	RDG_EVENT_SCOPE(GraphBuilder, "Nanite::EmitDepthTargets");
	RDG_GPU_STAT_SCOPE(GraphBuilder, NaniteDepth);

#if WITH_EDITORONLY_DATA
	// Hide all Nanite meshes when the isolate invalid coarse mesh batch debug mode is active.
	if (GNaniteIsolateInvalidCoarseMesh != 0)
	{
		const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);
		VisBuffer64 = SystemTextures.Black;
	}
#endif

	const FSceneTexturesConfig& Config = View.GetSceneTexturesConfig();
	const EShaderPlatform ShaderPlatform = View.GetShaderPlatform();
	const FIntPoint SceneTexturesExtent = Config.Extent;
	const FClearValueBinding DefaultDepthStencil = Config.DepthClearValue;

	float DefaultDepth = 0.0f;
	uint32 DefaultStencil = 0;
	DefaultDepthStencil.GetDepthStencil(DefaultDepth, DefaultStencil);

	const uint32 StencilDecalMask = GET_STENCIL_BIT_MASK(RECEIVE_DECAL, 1);

	const bool bEmitVelocity = VelocityBuffer != nullptr;
	const bool bClearVelocity = bEmitVelocity && !HasBeenProduced(VelocityBuffer);

	FRDGTextureDesc MaterialResolveDesc = FRDGTextureDesc::Create2D(
		SceneTexturesExtent,
		PF_R16_UINT,
		FClearValueBinding::Transparent,
		TexCreate_RenderTargetable | TexCreate_ShaderResource | TexCreate_UAV);

	// TODO: Can be 16bit UNORM (PF_ShadowDepth) (32bit float w/ 8bit stencil is a waste of bandwidth and memory)
	FRDGTextureDesc MaterialDepthDesc = FRDGTextureDesc::Create2D(
		SceneTexturesExtent,
		PF_DepthStencil,
		DefaultDepthStencil,
		TexCreate_DepthStencilTargetable | TexCreate_ShaderResource | TexCreate_InputAttachmentRead | (UseComputeDepthExport() ? TexCreate_UAV : TexCreate_NoFastClear));

	FRDGTextureRef MaterialResolve	= GraphBuilder.CreateTexture(MaterialResolveDesc, TEXT("Nanite.MaterialResolve"));
	FRDGTextureRef MaterialDepth	= GraphBuilder.CreateTexture(MaterialDepthDesc, TEXT("Nanite.MaterialDepth"));

	if (UseComputeDepthExport())
	{
		// Emit depth, stencil, mask and velocity

		{
			// HACK: Dummy pass to force depth decompression. Depth export shader needs to be refactored to handle already-compressed surfaces.
			FDummyDepthDecompressParameters* DummyParams = GraphBuilder.AllocParameters<FDummyDepthDecompressParameters>();
			DummyParams->SceneDepth = SceneDepth;

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("DummyDepthDecompress"),
				DummyParams,
				ERDGPassFlags::Copy | ERDGPassFlags::NeverCull,
				[](FRHICommandList&) {}
			);
		}

		// TODO: Don't currently support offset views.
		checkf(View.ViewRect.Min.X == 0 && View.ViewRect.Min.Y == 0, TEXT("Viewport offset support is not implemented."));

		const FIntVector DispatchDim = FComputeShaderUtils::GetGroupCount(View.ViewRect.Max, 8); // Only run DepthExport shader on viewport. We have already asserted that ViewRect.Min=0.
		const uint32 PlatformConfig = RHIGetHTilePlatformConfig(SceneTexturesExtent.X, SceneTexturesExtent.Y);

		FRDGTextureUAVRef SceneDepthUAV			= GraphBuilder.CreateUAV(FRDGTextureUAVDesc::CreateForMetaData(SceneDepth, ERDGTextureMetaDataAccess::CompressedSurface));
		FRDGTextureUAVRef SceneStencilUAV		= GraphBuilder.CreateUAV(FRDGTextureUAVDesc::CreateForMetaData(SceneDepth, ERDGTextureMetaDataAccess::Stencil));
		FRDGTextureUAVRef SceneHTileUAV			= GraphBuilder.CreateUAV(FRDGTextureUAVDesc::CreateForMetaData(SceneDepth, ERDGTextureMetaDataAccess::HTile));
		FRDGTextureUAVRef MaterialDepthUAV		= GraphBuilder.CreateUAV(FRDGTextureUAVDesc::CreateForMetaData(MaterialDepth, ERDGTextureMetaDataAccess::CompressedSurface));
		FRDGTextureUAVRef MaterialHTileUAV		= GraphBuilder.CreateUAV(FRDGTextureUAVDesc::CreateForMetaData(MaterialDepth, ERDGTextureMetaDataAccess::HTile));
		FRDGTextureUAVRef VelocityUAV			= bEmitVelocity ? GraphBuilder.CreateUAV(VelocityBuffer) : nullptr;
		FRDGTextureUAVRef MaterialResolveUAV	= GraphBuilder.CreateUAV(MaterialResolve);

		FDepthExportCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDepthExportCS::FParameters>();

		PassParameters->View					= View.ViewUniformBuffer;
		PassParameters->InViews					= GraphBuilder.CreateSRV(ViewsBuffer);
		PassParameters->VisibleClustersSWHW		= GraphBuilder.CreateSRV(VisibleClustersSWHW);
		PassParameters->PageConstants			= PageConstants;
		PassParameters->ClusterPageData			= Nanite::GStreamingManager.GetClusterPageDataSRV(GraphBuilder);
		PassParameters->DepthExportConfig		= FIntVector4(PlatformConfig, SceneTexturesExtent.X, StencilDecalMask, Nanite::FGlobalResources::GetMaxVisibleClusters());
		PassParameters->ViewRectMax				= FIntVector4(View.ViewRect.Max.X, View.ViewRect.Max.Y, 0, 0);
		PassParameters->VisBuffer64				= VisBuffer64;
		PassParameters->Velocity				= VelocityUAV;
		PassParameters->MaterialResolve			= MaterialResolveUAV;
		PassParameters->SceneHTile				= SceneHTileUAV;
		PassParameters->SceneDepth				= SceneDepthUAV;
		PassParameters->SceneStencil			= SceneStencilUAV;
		PassParameters->MaterialHTile			= MaterialHTileUAV;
		PassParameters->MaterialDepth			= MaterialDepthUAV;
		PassParameters->MaterialSlotTable		= Scene.NaniteMaterials[ENaniteMeshPass::BasePass].GetMaterialSlotSRV();
		PassParameters->MaterialDepthTable		= Scene.NaniteMaterials[ENaniteMeshPass::BasePass].GetMaterialDepthSRV();

		FDepthExportCS::FPermutationDomain PermutationVectorCS;
		PermutationVectorCS.Set<FDepthExportCS::FVelocityExportDim>(bEmitVelocity);
		auto ComputeShader = View.ShaderMap->GetShader<FDepthExportCS>(PermutationVectorCS);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("DepthExport"),
			ComputeShader,
			PassParameters,
			DispatchDim
		);
	}
	else
	{
		// Can't use ERenderTargetLoadAction::EClear to clear here because it needs to be the same for all render targets.
		AddClearRenderTargetPass(GraphBuilder, MaterialResolve);
		if (bClearVelocity)
		{
			AddClearRenderTargetPass(GraphBuilder, VelocityBuffer);
		}

		if (GRHISupportsStencilRefFromPixelShader)
		{
			// Emit scene depth, stencil, mask and velocity

			FEmitSceneDepthStencilPS::FPermutationDomain PermutationVectorPS;
			PermutationVectorPS.Set<FEmitSceneDepthStencilPS::FVelocityExportDim>(bEmitVelocity);
			auto  PixelShader = View.ShaderMap->GetShader<FEmitSceneDepthStencilPS>(PermutationVectorPS);
			
			auto* PassParameters = GraphBuilder.AllocParameters<FEmitSceneDepthStencilPS::FParameters>();

			PassParameters->View						= View.ViewUniformBuffer;
			PassParameters->InViews						= GraphBuilder.CreateSRV(ViewsBuffer);
			PassParameters->VisibleClustersSWHW			= GraphBuilder.CreateSRV(VisibleClustersSWHW);
			PassParameters->PageConstants				= PageConstants;
			PassParameters->StencilClear				= DefaultStencil;
			PassParameters->StencilDecal				= StencilDecalMask;
			PassParameters->ClusterPageData				= Nanite::GStreamingManager.GetClusterPageDataSRV(GraphBuilder);
			PassParameters->VisBuffer64					= VisBuffer64;
			PassParameters->MaterialSlotTable			= Scene.NaniteMaterials[ENaniteMeshPass::BasePass].GetMaterialSlotSRV();
			PassParameters->RenderTargets[0]			= FRenderTargetBinding(MaterialResolve, ERenderTargetLoadAction::ELoad);
			PassParameters->RenderTargets[1]			= bEmitVelocity ? FRenderTargetBinding(VelocityBuffer, ERenderTargetLoadAction::ELoad) : FRenderTargetBinding();
			PassParameters->RenderTargets.DepthStencil	= FDepthStencilBinding(SceneDepth, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilWrite);

			FPixelShaderUtils::AddFullscreenPass(
				GraphBuilder,
				View.ShaderMap,
				RDG_EVENT_NAME("Emit Scene Depth/Stencil/Resolve/Velocity"),
				PixelShader,
				PassParameters,
				View.ViewRect,
				TStaticBlendState<>::GetRHI(),
				TStaticRasterizerState<>::GetRHI(),
				TStaticDepthStencilState<true, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Keep, SO_Replace>::GetRHI()
			);
		}
		else
		{
			// Emit scene depth buffer, mask and velocity
			{
				FEmitSceneDepthPS::FPermutationDomain PermutationVectorPS;
				PermutationVectorPS.Set<FEmitSceneDepthPS::FVelocityExportDim>(bEmitVelocity);
				PermutationVectorPS.Set<FEmitSceneDepthPS::FMaterialResolveDim>(true);
				auto  PixelShader = View.ShaderMap->GetShader<FEmitSceneDepthPS>(PermutationVectorPS);
				
				auto* PassParameters = GraphBuilder.AllocParameters<FEmitSceneDepthPS::FParameters>();

				PassParameters->View						= View.ViewUniformBuffer;
				PassParameters->InViews						= GraphBuilder.CreateSRV(ViewsBuffer);
				PassParameters->VisibleClustersSWHW			= GraphBuilder.CreateSRV(VisibleClustersSWHW);
				PassParameters->PageConstants				= PageConstants;
				PassParameters->VisBuffer64					= VisBuffer64;
				PassParameters->ClusterPageData				= Nanite::GStreamingManager.GetClusterPageDataSRV(GraphBuilder);
				PassParameters->MaterialSlotTable			= Scene.NaniteMaterials[ENaniteMeshPass::BasePass].GetMaterialSlotSRV();
				PassParameters->RenderTargets[0]			= FRenderTargetBinding(MaterialResolve,	ERenderTargetLoadAction::ELoad);
				PassParameters->RenderTargets[1]			= bEmitVelocity ? FRenderTargetBinding(VelocityBuffer, ERenderTargetLoadAction::ELoad) : FRenderTargetBinding();
				PassParameters->RenderTargets.DepthStencil	= FDepthStencilBinding(SceneDepth, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilWrite);

				FPixelShaderUtils::AddFullscreenPass(
					GraphBuilder,
					View.ShaderMap,
					RDG_EVENT_NAME("Emit Scene Depth/Resolve/Velocity"),
					PixelShader,
					PassParameters,
					View.ViewRect,
					TStaticBlendState<>::GetRHI(),
					TStaticRasterizerState<>::GetRHI(),
					TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI()
				);
			}

			// Emit scene stencil
			{
				auto  PixelShader		= View.ShaderMap->GetShader<FEmitSceneStencilPS>();
				auto* PassParameters	= GraphBuilder.AllocParameters<FEmitSceneStencilPS::FParameters>();

				PassParameters->View						= View.ViewUniformBuffer;
				PassParameters->VisibleClustersSWHW			= GraphBuilder.CreateSRV(VisibleClustersSWHW);
				PassParameters->PageConstants				= PageConstants;
				PassParameters->ClusterPageData				= Nanite::GStreamingManager.GetClusterPageDataSRV(GraphBuilder);
				PassParameters->MaterialResolve				= MaterialResolve;
				PassParameters->VisBuffer64					= VisBuffer64;
				PassParameters->RenderTargets.DepthStencil	= FDepthStencilBinding
				(
					SceneDepth,
					ERenderTargetLoadAction::ELoad,
					FExclusiveDepthStencil::DepthWrite_StencilWrite
				);

				FPixelShaderUtils::AddFullscreenPass(
					GraphBuilder,
					View.ShaderMap,
					RDG_EVENT_NAME("Emit Scene Stencil"),
					PixelShader,
					PassParameters,
					View.ViewRect,
					TStaticBlendState<>::GetRHI(),
					TStaticRasterizerState<>::GetRHI(),
					TStaticDepthStencilState<false, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Keep, SO_Replace>::GetRHI(),
					StencilDecalMask | GET_STENCIL_BIT_MASK(DISTANCE_FIELD_REPRESENTATION, 1)
				);
			}
		}

		// Emit material depth (and stencil mask) for pixels produced from Nanite rasterization.
		{
			auto* PassParameters = GraphBuilder.AllocParameters<FEmitMaterialDepthPS::FParameters>();

			PassParameters->DummyZero = 0u;
			PassParameters->ClusterPageData				= Nanite::GStreamingManager.GetClusterPageDataSRV(GraphBuilder);
			PassParameters->MaterialSlotTable			= Scene.NaniteMaterials[ENaniteMeshPass::BasePass].GetMaterialSlotSRV();
			PassParameters->MaterialDepthTable			= Scene.NaniteMaterials[ENaniteMeshPass::BasePass].GetMaterialDepthSRV();
			PassParameters->PageConstants				= PageConstants;
			PassParameters->View						= View.ViewUniformBuffer;
			PassParameters->MaterialResolve				= MaterialResolve;
			PassParameters->VisBuffer64					= VisBuffer64;
			PassParameters->VisibleClustersSWHW			= GraphBuilder.CreateSRV(VisibleClustersSWHW);
			PassParameters->RenderTargets.DepthStencil	= FDepthStencilBinding(
				MaterialDepth,
				ERenderTargetLoadAction::EClear,
				FExclusiveDepthStencil::DepthWrite_StencilWrite
			);

			FEmitMaterialDepthPS::FPermutationDomain PermutationVectorPS;
			PermutationVectorPS.Set<FEmitMaterialDepthPS::FMaterialResolveDim>(true /* using material resolve */);
			auto PixelShader = View.ShaderMap->GetShader<FEmitMaterialDepthPS>(PermutationVectorPS);

			FRHIDepthStencilState* DepthStencilState = bStencilMask ?
				TStaticDepthStencilState<true, CF_Always, true, CF_Always, SO_Keep, SO_Keep, SO_Replace>::GetRHI() :
				TStaticDepthStencilState<true, CF_Always>::GetRHI();

			const uint32 StencilRef = bStencilMask ? uint32(STENCIL_SANDBOX_MASK) : 0u;

			FPixelShaderUtils::AddFullscreenPass(
				GraphBuilder,
				View.ShaderMap,
				RDG_EVENT_NAME("Emit Material Depth"),
				PixelShader,
				PassParameters,
				View.ViewRect,
				TStaticBlendState<>::GetRHI(),
				TStaticRasterizerState<>::GetRHI(),
				DepthStencilState,
				StencilRef
			);
		}

		if (GRHISupportsResummarizeHTile && GNaniteResummarizeHTile != 0)
		{
			// Resummarize HTile meta data if the RHI supports it
			AddResummarizeHTilePass(GraphBuilder, SceneDepth);
		}
	}

	OutMaterialResolve = MaterialResolve;
	OutMaterialDepth = MaterialDepth;
}

struct FLumenMeshCaptureMaterialPassIndex
{
	FLumenMeshCaptureMaterialPassIndex(int32 InIndex, int32 InCommandStateBucketId)
		: Index(InIndex)
		, CommandStateBucketId(InCommandStateBucketId)
	{
	}

	inline friend uint32 GetTypeHash(const FLumenMeshCaptureMaterialPassIndex& PassIndex)
	{
		return CityHash32((const char*)&PassIndex.CommandStateBucketId, sizeof(PassIndex.CommandStateBucketId));
	}

	inline bool operator==(const FLumenMeshCaptureMaterialPassIndex& PassIndex) const
	{
		return CommandStateBucketId == PassIndex.CommandStateBucketId;
	}

	int32 Index = -1;
	int32 CommandStateBucketId = -1;
};

struct FLumenMeshCaptureMaterialPass
{
	uint64 SortKey = 0;
	int32 CommandStateBucketId = INDEX_NONE;
	uint32 ViewIndexBufferOffset = 0;
	TArray<uint16, TInlineAllocator<64>> ViewIndices;

	inline float GetMaterialDepth() const
	{
		return FNaniteCommandInfo::GetDepthId(CommandStateBucketId);
	}

	bool operator<(const FLumenMeshCaptureMaterialPass& Other) const
	{
		return SortKey < Other.SortKey;
	}
};

void DrawLumenMeshCapturePass(
	FRDGBuilder& GraphBuilder,
	FScene& Scene,
	FViewInfo* SharedView,
	TArrayView<const FCardPageRenderData> CardPagesToRender,
	const FCullingContext& CullingContext,
	const FRasterContext& RasterContext,
	FLumenCardPassUniformParameters* PassUniformParameters,
	FRDGBufferSRVRef RectMinMaxBufferSRV,
	uint32 NumRects,
	FIntPoint ViewportSize,
	FRDGTextureRef AlbedoAtlasTexture,
	FRDGTextureRef NormalAtlasTexture,
	FRDGTextureRef EmissiveAtlasTexture,
	FRDGTextureRef DepthAtlasTexture
)
{
	checkSlow(DoesPlatformSupportNanite(GMaxRHIShaderPlatform));
	checkSlow(DoesPlatformSupportLumenGI(GMaxRHIShaderPlatform));

	LLM_SCOPE_BYTAG(Nanite);
	RDG_EVENT_SCOPE(GraphBuilder, "Nanite::DrawLumenMeshCapturePass");

	const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);

	// Material tile remap buffer (currently not used by Lumen, but still must be bound)
	FRDGBufferRef MaterialTileRemap = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(4, 1), TEXT("Nanite.MaterialTileRemap"));
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(MaterialTileRemap), 0);

	// Mark stencil for all pixels that pass depth test
	{
		FNaniteMarkStencilRectsParameters* PassParameters = GraphBuilder.AllocParameters<FNaniteMarkStencilRectsParameters>();

		PassParameters->PS.View = SharedView->ViewUniformBuffer;
		PassParameters->PS.VisBuffer64 = RasterContext.VisBuffer64;

		PassParameters->PS.RenderTargets.DepthStencil = FDepthStencilBinding(
			DepthAtlasTexture,
			ERenderTargetLoadAction::ELoad,
			ERenderTargetLoadAction::ELoad,
			FExclusiveDepthStencil::DepthRead_StencilWrite
		);
		
		auto PixelShader = SharedView->ShaderMap->GetShader<FNaniteMarkStencilPS>();

		FPixelShaderUtils::AddRasterizeToRectsPass(GraphBuilder,
			SharedView->ShaderMap,
			RDG_EVENT_NAME("Mark Stencil"),
			PixelShader,
			PassParameters,
			ViewportSize,
			RectMinMaxBufferSRV,
			NumRects,
			TStaticBlendState<>::GetRHI(),
			TStaticRasterizerState<>::GetRHI(),
			TStaticDepthStencilState<false, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Keep, SO_Replace>::GetRHI(),
			STENCIL_SANDBOX_MASK
		);
	}

	// Emit material IDs as depth values
	{
		FNaniteEmitMaterialIdRectsParameters* PassParameters = GraphBuilder.AllocParameters<FNaniteEmitMaterialIdRectsParameters>();

		PassParameters->PS.View = SharedView->ViewUniformBuffer;
		PassParameters->PS.DummyZero = 0u;

		PassParameters->PS.VisibleClustersSWHW = GraphBuilder.CreateSRV(CullingContext.VisibleClustersSWHW);
		PassParameters->PS.PageConstants = CullingContext.PageConstants;
		PassParameters->PS.ClusterPageData = GStreamingManager.GetClusterPageDataSRV(GraphBuilder);

		PassParameters->PS.VisBuffer64 = RasterContext.VisBuffer64;

		PassParameters->PS.MaterialSlotTable	= Scene.NaniteMaterials[ENaniteMeshPass::LumenCardCapture].GetMaterialSlotSRV();
		PassParameters->PS.MaterialDepthTable	= Scene.NaniteMaterials[ENaniteMeshPass::LumenCardCapture].GetMaterialDepthSRV();

		PassParameters->PS.RenderTargets.DepthStencil = FDepthStencilBinding(
			DepthAtlasTexture,
			ERenderTargetLoadAction::ELoad,
			ERenderTargetLoadAction::ELoad,
			FExclusiveDepthStencil::DepthWrite_StencilRead
		);

		FEmitMaterialDepthPS::FPermutationDomain PermutationVectorPS;
		PermutationVectorPS.Set<FEmitMaterialDepthPS::FMaterialResolveDim>(false /* not using material resolve */);
		auto PixelShader = SharedView->ShaderMap->GetShader<FEmitMaterialDepthPS>(PermutationVectorPS);

		FPixelShaderUtils::AddRasterizeToRectsPass(GraphBuilder,
			SharedView->ShaderMap,
			RDG_EVENT_NAME("Emit Material Depth"),
			PixelShader,
			PassParameters,
			ViewportSize,
			RectMinMaxBufferSRV,
			NumRects,
			TStaticBlendState<>::GetRHI(),
			TStaticRasterizerState<>::GetRHI(),
			TStaticDepthStencilState<true, CF_Always, true, CF_Equal>::GetRHI(),
			STENCIL_SANDBOX_MASK
		);
	}

	// Emit GBuffer Values
	{
		FNaniteEmitGBufferParameters* PassParameters = GraphBuilder.AllocParameters<FNaniteEmitGBufferParameters>();

		PassParameters->RenderTargets[0] = FRenderTargetBinding(AlbedoAtlasTexture, ERenderTargetLoadAction::ELoad);
		PassParameters->RenderTargets[1] = FRenderTargetBinding(NormalAtlasTexture, ERenderTargetLoadAction::ELoad);
		PassParameters->RenderTargets[2] = FRenderTargetBinding(EmissiveAtlasTexture, ERenderTargetLoadAction::ELoad);

		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
			DepthAtlasTexture,
			ERenderTargetLoadAction::ELoad,
			ERenderTargetLoadAction::ELoad,
			FExclusiveDepthStencil::DepthWrite_StencilRead
		);

		int32 NumMaterialQuads = 0;
		TArray<FLumenMeshCaptureMaterialPass, SceneRenderingAllocator> MaterialPasses;
		MaterialPasses.Reserve(CardPagesToRender.Num());

		// Build list of unique materials
		{
			FGraphicsPipelineRenderTargetsInfo RenderTargetsInfo = ExtractRenderTargetsInfo(FRDGParameterStruct(PassParameters, FNaniteEmitGBufferParameters::FTypeInfo::GetStructMetadata()));

			Experimental::TRobinHoodHashSet<FLumenMeshCaptureMaterialPassIndex> MaterialPassSet;

			for (int32 CardPageIndex = 0; CardPageIndex < CardPagesToRender.Num(); ++CardPageIndex)
			{
				const FCardPageRenderData& CardPageRenderData = CardPagesToRender[CardPageIndex];

				for (const FNaniteCommandInfo& CommandInfo : CardPageRenderData.NaniteCommandInfos)
				{
					const FLumenMeshCaptureMaterialPassIndex& PassIndex = *MaterialPassSet.FindOrAdd(FLumenMeshCaptureMaterialPassIndex(MaterialPasses.Num(), CommandInfo.GetStateBucketId()));

					if (PassIndex.Index >= MaterialPasses.Num())
					{
						const FNaniteMaterialCommands& LumenMaterialCommands = Scene.NaniteMaterials[ENaniteMeshPass::LumenCardCapture];
						FNaniteMaterialCommands::FCommandId CommandId(CommandInfo.GetStateBucketId());
						const FMeshDrawCommand& MeshDrawCommand = LumenMaterialCommands.GetCommand(CommandId);

						FLumenMeshCaptureMaterialPass MaterialPass;
						MaterialPass.SortKey = MeshDrawCommand.GetPipelineStateSortingKey(GraphBuilder.RHICmdList, RenderTargetsInfo);
						MaterialPass.CommandStateBucketId = CommandInfo.GetStateBucketId();
						MaterialPass.ViewIndexBufferOffset = 0;
						MaterialPasses.Add(MaterialPass);
					}

					MaterialPasses[PassIndex.Index].ViewIndices.Add(CardPageIndex);
					++NumMaterialQuads;
				}
			}
			ensure(MaterialPasses.Num() > 0);
		}

		if (MaterialPasses.Num() > 0)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Sort);
			MaterialPasses.Sort();
		}

		TArray<uint32, SceneRenderingAllocator> ViewIndices;
		ViewIndices.Reserve(NumMaterialQuads);

		for (FLumenMeshCaptureMaterialPass& MaterialPass : MaterialPasses)
		{
			MaterialPass.ViewIndexBufferOffset = ViewIndices.Num();

			for (int32 ViewIndex : MaterialPass.ViewIndices)
			{
				ViewIndices.Add(ViewIndex);
			}
		}
		ensure(ViewIndices.Num() > 0);

		FRDGBufferRef ViewIndexBuffer = CreateStructuredBuffer(
			GraphBuilder, 
			TEXT("Nanite.ViewIndices"),
			ViewIndices.GetTypeSize(),
			FMath::RoundUpToPowerOfTwo(ViewIndices.Num()),
			ViewIndices.GetData(),
			ViewIndices.Num() * ViewIndices.GetTypeSize());

		TArray<FVector4f, SceneRenderingAllocator> ViewRectScaleOffsets;
		ViewRectScaleOffsets.Reserve(CardPagesToRender.Num());

		TArray<Nanite::FPackedView, SceneRenderingAllocator> PackedViews;
		PackedViews.Reserve(CardPagesToRender.Num());

		const FVector2f ViewportSizeF = FVector2f(float(ViewportSize.X), float(ViewportSize.Y));

		for (const FCardPageRenderData& CardPageRenderData : CardPagesToRender)
		{
			const FVector2f CardViewportSize = FVector2f(float(CardPageRenderData.CardCaptureAtlasRect.Width()), float(CardPageRenderData.CardCaptureAtlasRect.Height()));
			const FVector2f RectOffset = FVector2f(float(CardPageRenderData.CardCaptureAtlasRect.Min.X), float(CardPageRenderData.CardCaptureAtlasRect.Min.Y)) / ViewportSizeF;
			const FVector2f RectScale = CardViewportSize / ViewportSizeF;

			ViewRectScaleOffsets.Add(FVector4f(RectScale, RectOffset));

			Nanite::FPackedViewParams Params;
			Params.ViewMatrices = CardPageRenderData.ViewMatrices;
			Params.PrevViewMatrices = CardPageRenderData.ViewMatrices;
			Params.ViewRect = CardPageRenderData.CardCaptureAtlasRect;
			Params.RasterContextSize = ViewportSize;
			Params.LODScaleFactor = 0.0f;
			PackedViews.Add(Nanite::CreatePackedView(Params));
		}

		FRDGBufferRef ViewRectScaleOffsetBuffer = CreateStructuredBuffer(
			GraphBuilder,
			TEXT("Nanite.ViewRectScaleOffset"),
			ViewRectScaleOffsets.GetTypeSize(),
			FMath::RoundUpToPowerOfTwo(ViewRectScaleOffsets.Num()),
			ViewRectScaleOffsets.GetData(),
			ViewRectScaleOffsets.Num() * ViewRectScaleOffsets.GetTypeSize());

		FRDGBufferRef PackedViewBuffer = CreateStructuredBuffer(
			GraphBuilder,
			TEXT("Nanite.PackedViews"),
			PackedViews.GetTypeSize(),
			FMath::RoundUpToPowerOfTwo(PackedViews.Num()),
			PackedViews.GetData(),
			PackedViews.Num() * PackedViews.GetTypeSize());

		PassParameters->PageConstants = CullingContext.PageConstants;
		PassParameters->MaxVisibleClusters = Nanite::FGlobalResources::GetMaxVisibleClusters();
		PassParameters->MaxNodes = Nanite::FGlobalResources::GetMaxNodes();
		PassParameters->RenderFlags = CullingContext.RenderFlags;
		PassParameters->RayTracingCutError = GRayTracingCutError;

		PassParameters->ClusterPageData = GStreamingManager.GetClusterPageDataSRV(GraphBuilder);
		PassParameters->HierarchyBuffer = Nanite::GStreamingManager.GetHierarchySRV(GraphBuilder);

		PassParameters->VisibleClustersSWHW = GraphBuilder.CreateSRV(CullingContext.VisibleClustersSWHW);

		PassParameters->GridSize = { 1u, 1u };

		PassParameters->MaterialTileRemap = GraphBuilder.CreateSRV(MaterialTileRemap, PF_R32_UINT); // Dummy

		PassParameters->MultiViewEnabled = 1;
		PassParameters->MultiViewIndices = GraphBuilder.CreateSRV(ViewIndexBuffer);
		PassParameters->MultiViewRectScaleOffsets = GraphBuilder.CreateSRV(ViewRectScaleOffsetBuffer);
		PassParameters->InViews = GraphBuilder.CreateSRV(PackedViewBuffer);

		PassParameters->VisBuffer64 = RasterContext.VisBuffer64;
		PassParameters->DbgBuffer64 = SystemTextures.Black;
		PassParameters->DbgBuffer32 = SystemTextures.Black;

		PassParameters->View = Scene.UniformBuffers.LumenCardCaptureViewUniformBuffer;
		PassParameters->CardPass = GraphBuilder.CreateUniformBuffer(PassUniformParameters);

		TShaderMapRef<FNaniteMultiViewMaterialVS> NaniteVertexShader(SharedView->ShaderMap);

		GraphBuilder.AddPass
		(
			RDG_EVENT_NAME("Lumen Emit GBuffer %d materials %d quads", MaterialPasses.Num(), NumMaterialQuads),
			PassParameters,
			ERDGPassFlags::Raster,
			[PassParameters, 
				&Scene, 
				&FirstCardPage = CardPagesToRender[0],
				MaterialPassArray = TArrayView<const FLumenMeshCaptureMaterialPass>(MaterialPasses),
				NaniteVertexShader, 
				SharedView,
				ViewportSize](FRHICommandListImmediate& RHICmdList)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(LumenEmitGBuffer);

				FirstCardPage.PatchView(&Scene, SharedView);
				Scene.UniformBuffers.LumenCardCaptureViewUniformBuffer.UpdateUniformBufferImmediate(*SharedView->CachedViewUniformShaderParameters);

				FNaniteUniformParameters UniformParams;
				UniformParams.PageConstants = PassParameters->PageConstants;
				UniformParams.MaxVisibleClusters = PassParameters->MaxVisibleClusters;
				UniformParams.MaxNodes = PassParameters->MaxNodes;
				UniformParams.RenderFlags = PassParameters->RenderFlags;
				UniformParams.RayTracingCutError = PassParameters->RayTracingCutError;
				UniformParams.MaterialConfig = FIntVector4(0, 1, 1, 0); // Tile based material culling is not required for Lumen, as each card is rendered as a small rect
				UniformParams.RectScaleOffset = FVector4f(1.0f, 1.0f, 0.0f, 0.0f); // This will be overridden in vertex shader

				UniformParams.ClusterPageData = PassParameters->ClusterPageData->GetRHI();
				UniformParams.HierarchyBuffer = PassParameters->HierarchyBuffer->GetRHI();

				UniformParams.VisibleClustersSWHW = PassParameters->VisibleClustersSWHW->GetRHI();

				UniformParams.MaterialTileRemap = PassParameters->MaterialTileRemap->GetRHI();

				UniformParams.MultiViewEnabled = PassParameters->MultiViewEnabled;
				UniformParams.MultiViewIndices = PassParameters->MultiViewIndices->GetRHI();
				UniformParams.MultiViewRectScaleOffsets = PassParameters->MultiViewRectScaleOffsets->GetRHI();
				UniformParams.InViews = PassParameters->InViews->GetRHI();

				UniformParams.VisBuffer64 = PassParameters->VisBuffer64->GetRHI();
				UniformParams.DbgBuffer64 = PassParameters->DbgBuffer64->GetRHI();
				UniformParams.DbgBuffer32 = PassParameters->DbgBuffer32->GetRHI();

				Scene.UniformBuffers.NaniteUniformBuffer.UpdateUniformBufferImmediate(UniformParams);

				FGraphicsMinimalPipelineStateSet GraphicsMinimalPipelineStateSet;
				FMeshDrawCommandStateCache StateCache;

				const FNaniteMaterialCommands& LumenMaterialCommands = Scene.NaniteMaterials[ENaniteMeshPass::LumenCardCapture];
				for (const FLumenMeshCaptureMaterialPass& MaterialPass : MaterialPassArray)
				{
					// One instance per card page
					const uint32 InstanceFactor = MaterialPass.ViewIndices.Num();
					const uint32 InstanceBaseOffset = MaterialPass.ViewIndexBufferOffset;

					FNaniteMaterialCommands::FCommandId CommandId(MaterialPass.CommandStateBucketId);
					const FMeshDrawCommand& MeshDrawCommand = LumenMaterialCommands.GetCommand(CommandId);
					const float MaterialDepth = MaterialPass.GetMaterialDepth();

					SubmitNaniteMultiViewMaterial(
						MeshDrawCommand,
						MaterialDepth,
						NaniteVertexShader,
						GraphicsMinimalPipelineStateSet,
						InstanceFactor,
						RHICmdList,
						StateCache,
						InstanceBaseOffset
					);
				}
			}
		);
	}

	// Emit depth values
	{
		FNaniteEmitDepthRectsParameters* PassParameters = GraphBuilder.AllocParameters<FNaniteEmitDepthRectsParameters>();

		PassParameters->PS.VisBuffer64 = RasterContext.VisBuffer64;
		PassParameters->PS.RenderTargets.DepthStencil = FDepthStencilBinding(
			DepthAtlasTexture,
			ERenderTargetLoadAction::ELoad,
			ERenderTargetLoadAction::ELoad,
			FExclusiveDepthStencil::DepthWrite_StencilRead
		);

		FEmitSceneDepthPS::FPermutationDomain PermutationVectorPS;
		PermutationVectorPS.Set<FEmitSceneDepthPS::FVelocityExportDim>(false);
		PermutationVectorPS.Set<FEmitSceneDepthPS::FMaterialResolveDim>(false);
		auto PixelShader = SharedView->ShaderMap->GetShader<FEmitSceneDepthPS>(PermutationVectorPS);

		FPixelShaderUtils::AddRasterizeToRectsPass(GraphBuilder,
			SharedView->ShaderMap,
			RDG_EVENT_NAME("Emit Depth"),
			PixelShader,
			PassParameters,
			ViewportSize,
			RectMinMaxBufferSRV,
			NumRects,
			TStaticBlendState<>::GetRHI(),
			TStaticRasterizerState<>::GetRHI(),
			TStaticDepthStencilState<true, CF_Always, true, CF_Equal>::GetRHI(),
			STENCIL_SANDBOX_MASK
		);
	}
}

} // namespace Nanite

FNaniteMaterialCommands::FNaniteMaterialCommands(uint32 InMaxMaterials)
: MaxMaterials(InMaxMaterials)
{
	check(MaxMaterials > 0);
}

FNaniteMaterialCommands::~FNaniteMaterialCommands()
{
	Release();
}

void FNaniteMaterialCommands::Release()
{
	HitProxyTableUploadBuffer.Release();
	HitProxyTableDataBuffer = nullptr;

	MaterialSlotUploadBuffer.Release();
	MaterialSlotDataBuffer = nullptr;

	MaterialDepthUploadBuffer.Release();
	MaterialDepthDataBuffer = nullptr;

#if WITH_DEBUG_VIEW_MODES
	MaterialEditorUploadBuffer.Release();
	MaterialEditorDataBuffer = nullptr;
#endif
}

FNaniteCommandInfo FNaniteMaterialCommands::Register(FMeshDrawCommand& Command, FCommandHash CommandHash, uint32 InstructionCount)
{
	FNaniteCommandInfo CommandInfo;

	FCommandId CommandId = FindOrAddIdByHash(CommandHash, Command);
	
	CommandInfo.SetStateBucketId(CommandId.GetIndex());

	FNaniteMaterialEntry& MaterialEntry = GetPayload(CommandId);
	if (MaterialEntry.ReferenceCount == 0)
	{
		check(MaterialEntry.MaterialSlot == INDEX_NONE);
		MaterialEntry.MaterialSlot = MaterialSlotAllocator.Allocate(1);
		MaterialEntry.MaterialId = CommandInfo.GetMaterialId();
	#if WITH_DEBUG_VIEW_MODES
		MaterialEntry.InstructionCount = InstructionCount;
	#endif
		MaterialEntry.bNeedUpload = true;

		++NumMaterialDepthUpdates;
	}

	CommandInfo.SetMaterialSlot(MaterialEntry.MaterialSlot);

	++MaterialEntry.ReferenceCount;

	check(CommandInfo.GetMaterialSlot() != INDEX_NONE);
	return CommandInfo;
}

void FNaniteMaterialCommands::Unregister(const FNaniteCommandInfo& CommandInfo)
{
	if (CommandInfo.GetStateBucketId() == INDEX_NONE)
	{
		return;
	}

	const FMeshDrawCommand& MeshDrawCommand = GetCommand(CommandInfo.GetStateBucketId());
	FGraphicsMinimalPipelineStateId CachedPipelineId = MeshDrawCommand.CachedPipelineId;

	FNaniteMaterialEntry& MaterialEntry = GetPayload(CommandInfo.GetStateBucketId());
	check(MaterialEntry.ReferenceCount > 0);
	check(MaterialEntry.MaterialSlot != INDEX_NONE);

	--MaterialEntry.ReferenceCount;
	if (MaterialEntry.ReferenceCount == 0)
	{
		check(MaterialEntry.MaterialSlot != INDEX_NONE);
		MaterialSlotAllocator.Free(MaterialEntry.MaterialSlot, 1);

		MaterialEntry.MaterialSlot = INDEX_NONE;
	#if WITH_DEBUG_VIEW_MODES
		MaterialEntry.InstructionCount = 0;
	#endif

		if (MaterialEntry.bNeedUpload)
		{
			check(NumMaterialDepthUpdates > 0);
			--NumMaterialDepthUpdates;
			MaterialEntry.bNeedUpload = false;
		}
		
		RemoveById(CommandInfo.GetStateBucketId());
	}

	FGraphicsMinimalPipelineStateId::RemovePersistentId(CachedPipelineId);
}

void FNaniteMaterialCommands::UpdateBufferState(FRDGBuilder& GraphBuilder, uint32 NumPrimitives)
{
	checkSlow(DoesPlatformSupportNanite(GMaxRHIShaderPlatform));

	LLM_SCOPE_BYTAG(Nanite);

	check(NumPrimitiveUpdates == 0);
	check(NumMaterialSlotUpdates == 0);
#if WITH_EDITOR
	check(NumHitProxyTableUpdates == 0);
#endif

	TArray<FRHITransitionInfo, TInlineAllocator<2>> UAVs;

	const uint32 NumMaterialSlots = MaterialSlotAllocator.GetMaxSize();

	const uint32 PrimitiveUpdateReserve = FMath::RoundUpToPowerOfTwo(FMath::Max(NumPrimitives * MaxMaterials, 256u));
	const uint32 MaterialSlotReserve = FMath::RoundUpToPowerOfTwo(FMath::Max(NumMaterialSlots, 256u));

#if WITH_EDITOR
	ResizeByteAddressBufferIfNeeded(GraphBuilder, HitProxyTableDataBuffer, PrimitiveUpdateReserve * sizeof(uint32), TEXT("Nanite.HitProxyTableDataBuffer"));
#endif

	ResizeByteAddressBufferIfNeeded(GraphBuilder, MaterialSlotDataBuffer, PrimitiveUpdateReserve * sizeof(uint32), TEXT("Nanite.MaterialSlotDataBuffer"));

	ResizeByteAddressBufferIfNeeded(GraphBuilder, MaterialDepthDataBuffer, MaterialSlotReserve * sizeof(uint32), TEXT("Nanite.MaterialDepthDataBuffer"));

#if WITH_DEBUG_VIEW_MODES
	ResizeByteAddressBufferIfNeeded(GraphBuilder, MaterialEditorDataBuffer, MaterialSlotReserve * sizeof(uint32), TEXT("Nanite.MaterialEditorDataBuffer"));
#endif
}

void FNaniteMaterialCommands::Begin(FRDGBuilder& GraphBuilder, uint32 NumPrimitives, uint32 InNumPrimitiveUpdates)
{
	checkSlow(DoesPlatformSupportNanite(GMaxRHIShaderPlatform));

	LLM_SCOPE_BYTAG(Nanite);

	check(NumPrimitiveUpdates == 0);
	check(NumMaterialSlotUpdates == 0);

	const uint32 NumMaterialSlots = MaterialSlotAllocator.GetMaxSize();

	const uint32 PrimitiveUpdateReserve = FMath::RoundUpToPowerOfTwo(FMath::Max(NumPrimitives * MaxMaterials, 256u));
	const uint32 MaterialSlotReserve = FMath::RoundUpToPowerOfTwo(FMath::Max(NumMaterialSlots, 256u));

#if WITH_EDITOR
	check(NumHitProxyTableUpdates == 0);
	check(HitProxyTableDataBuffer);
	check(HitProxyTableDataBuffer->GetSize() == PrimitiveUpdateReserve * sizeof(uint32));
#endif
#if WITH_DEBUG_VIEW_MODES
	check(MaterialEditorDataBuffer)
	check(MaterialEditorDataBuffer->GetSize() == MaterialSlotReserve * sizeof(uint32));
#endif
	check(MaterialSlotDataBuffer);
	check(MaterialSlotDataBuffer->GetSize() == PrimitiveUpdateReserve * sizeof(uint32));
	check(MaterialDepthDataBuffer);
	check(MaterialDepthDataBuffer->GetSize() == MaterialSlotReserve * sizeof(uint32));

	NumPrimitiveUpdates = InNumPrimitiveUpdates;
	if (NumPrimitiveUpdates > 0)
	{
		MaterialSlotUploadBuffer.Init(GraphBuilder, NumPrimitiveUpdates * MaxMaterials, sizeof(uint32), false, TEXT("Nanite.MaterialSlotUploadBuffer"));
	#if WITH_EDITOR
		HitProxyTableUploadBuffer.Init(GraphBuilder, NumPrimitiveUpdates * MaxMaterials, sizeof(uint32), false, TEXT("Nanite.HitProxyTableUploadBuffer"));
	#endif
	}

	if (NumMaterialDepthUpdates > 0)
	{
		MaterialDepthUploadBuffer.Init(GraphBuilder, NumMaterialDepthUpdates, sizeof(uint32), false, TEXT("Nanite.MaterialDepthUploadBuffer"));
	#if WITH_DEBUG_VIEW_MODES
		MaterialEditorUploadBuffer.Init(GraphBuilder, NumMaterialDepthUpdates, sizeof(uint32), false, TEXT("Nanite.MaterialEditorUploadBuffer"));
	#endif

		for (auto& Command : EntryMap)
		{
			FNaniteMaterialEntry& MaterialEntry = Command.Value;
			if (MaterialEntry.bNeedUpload)
			{
				check(MaterialEntry.MaterialSlot != INDEX_NONE);
				*static_cast<uint32*>(MaterialDepthUploadBuffer.Add_GetRef(MaterialEntry.MaterialSlot)) = MaterialEntry.MaterialId;
			#if WITH_DEBUG_VIEW_MODES
				*static_cast<uint32*>(MaterialEditorUploadBuffer.Add_GetRef(MaterialEntry.MaterialSlot)) = MaterialEntry.InstructionCount;
			#endif
				MaterialEntry.bNeedUpload = false;
			}
		}
	}
}

void* FNaniteMaterialCommands::GetMaterialSlotPtr(uint32 PrimitiveIndex, uint32 EntryCount)
{
	++NumMaterialSlotUpdates;
	const uint32 BaseIndex = PrimitiveIndex * MaxMaterials;
	return MaterialSlotUploadBuffer.Add_GetRef(BaseIndex, EntryCount);
}

#if WITH_EDITOR
void* FNaniteMaterialCommands::GetHitProxyTablePtr(uint32 PrimitiveIndex, uint32 EntryCount)
{
	++NumHitProxyTableUpdates;
	const uint32 BaseIndex = PrimitiveIndex * MaxMaterials;
	return HitProxyTableUploadBuffer.Add_GetRef(BaseIndex, EntryCount);
}
#endif

void FNaniteMaterialCommands::Finish(FRDGBuilder& GraphBuilder, FRDGExternalAccessQueue& ExternalAccessQueue)
{
	checkSlow(DoesPlatformSupportNanite(GMaxRHIShaderPlatform));

	LLM_SCOPE_BYTAG(Nanite);

#if WITH_EDITOR
	check(NumHitProxyTableUpdates <= NumPrimitiveUpdates);
#endif
	check(NumMaterialSlotUpdates <= NumPrimitiveUpdates);

	if (NumPrimitiveUpdates == 0 && NumMaterialDepthUpdates == 0)
	{
		return;
	}

	RDG_EVENT_SCOPE(GraphBuilder, "UpdateNaniteMaterials PrimitiveUpdate = %u, MaterialUpdate = %u", NumPrimitiveUpdates, NumMaterialDepthUpdates);

	const auto Register = [&](const TRefCountPtr<FRDGPooledBuffer>& PooledBuffer)
	{
		FRDGBuffer* Buffer = GraphBuilder.RegisterExternalBuffer(PooledBuffer);
		ExternalAccessQueue.Add(Buffer);
		return Buffer;
	};

	FRDGBuffer* MaterialSlotDataBufferRDG = Register(MaterialSlotDataBuffer);
	FRDGBuffer* MaterialDepthDataBufferRDG = Register(MaterialDepthDataBuffer);
#if WITH_EDITOR
	FRDGBuffer* HitProxyTableDataBufferRDG = Register(HitProxyTableDataBuffer);
#endif
#if WITH_DEBUG_VIEW_MODES
	FRDGBuffer* MaterialEditorDataBufferRDG = Register(MaterialEditorDataBuffer);
#endif

	if (NumPrimitiveUpdates > 0)
	{
		MaterialSlotUploadBuffer.ResourceUploadTo(GraphBuilder, MaterialSlotDataBufferRDG);
#if WITH_EDITOR
		HitProxyTableUploadBuffer.ResourceUploadTo(GraphBuilder, HitProxyTableDataBufferRDG);
	#endif
	}

	if (NumMaterialDepthUpdates > 0)
	{
		MaterialDepthUploadBuffer.ResourceUploadTo(GraphBuilder, MaterialDepthDataBufferRDG);
	#if WITH_DEBUG_VIEW_MODES
		MaterialEditorUploadBuffer.ResourceUploadTo(GraphBuilder, MaterialEditorDataBufferRDG);
	#endif
	}

	NumMaterialSlotUpdates = 0;
#if WITH_EDITOR
	NumHitProxyTableUpdates = 0;
#endif
	NumPrimitiveUpdates = 0;
	NumMaterialDepthUpdates = 0;
}

FNaniteRasterPipelines::FNaniteRasterPipelines()
{
	PipelineBins.Reserve(256);
	PerPixelEvalPipelineBins.Reserve(256);
	PipelineMap.Reserve(256);
}

FNaniteRasterPipelines::~FNaniteRasterPipelines()
{
	PipelineBins.Reset();
	PerPixelEvalPipelineBins.Reset();
	PipelineMap.Empty();
}

uint16 FNaniteRasterPipelines::AllocateBin(bool bPerPixelEval)
{
	TBitArray<>& BinUsageMask = bPerPixelEval ? PerPixelEvalPipelineBins : PipelineBins;
	int32 BinIndex = BinUsageMask.FindAndSetFirstZeroBit();
	if (BinIndex == INDEX_NONE)
	{
		BinIndex = BinUsageMask.Add(true);
	}

	check(int32(uint16(BinIndex)) == BinIndex && PipelineBins.Num() + PerPixelEvalPipelineBins.Num() <= int32(MAX_uint16));
	return bPerPixelEval ? RevertBinIndex(BinIndex) : uint16(BinIndex);
}

void FNaniteRasterPipelines::ReleaseBin(uint16 BinIndex)
{
	check(IsBinAllocated(BinIndex));
	if (BinIndex < PipelineBins.Num())
	{
		PipelineBins[BinIndex] = false;
	}
	else
	{
		PerPixelEvalPipelineBins[RevertBinIndex(BinIndex)] = false;
	}
}

bool FNaniteRasterPipelines::IsBinAllocated(uint16 BinIndex) const
{
	return BinIndex < PipelineBins.Num() ? PipelineBins[BinIndex] : PerPixelEvalPipelineBins[RevertBinIndex(BinIndex)];
}

uint32 FNaniteRasterPipelines::GetRegularBinCount() const
{
	return PipelineBins.FindLast(true) + 1;
}

uint32 FNaniteRasterPipelines::GetBinCount() const
{
	return GetRegularBinCount() + PerPixelEvalPipelineBins.FindLast(true) + 1;
}

FNaniteRasterBin FNaniteRasterPipelines::Register(const FNaniteRasterPipeline& InRasterPipeline)
{
	FNaniteRasterBin RasterBin;

	const FRasterHash RasterPipelineHash = PipelineMap.ComputeHash(InRasterPipeline);
	FRasterId RasterBinId = PipelineMap.FindOrAddIdByHash(RasterPipelineHash, InRasterPipeline, FNaniteRasterEntry());
	RasterBin.BinId = RasterBinId.GetIndex();

	FNaniteRasterEntry& RasterEntry = PipelineMap.GetByElementId(RasterBinId).Value;
	if (RasterEntry.ReferenceCount == 0)
	{
		// First reference
		RasterEntry.RasterPipeline = InRasterPipeline;
		RasterEntry.BinIndex = AllocateBin(InRasterPipeline.bPerPixelEval);
	}

	++RasterEntry.ReferenceCount;

	RasterBin.BinIndex = RasterEntry.BinIndex;
	return RasterBin;
}

void FNaniteRasterPipelines::Unregister(const FNaniteRasterBin& InRasterBin)
{
	FRasterId RasterBinId(InRasterBin.BinId);
	check(RasterBinId.IsValid());

	FNaniteRasterEntry& RasterEntry = PipelineMap.GetByElementId(RasterBinId).Value;
	
	check(RasterEntry.ReferenceCount > 0);
	--RasterEntry.ReferenceCount;
	if (RasterEntry.ReferenceCount == 0)
	{
		ReleaseBin(RasterEntry.BinIndex);
		PipelineMap.RemoveByElementId(RasterBinId);
	}
}
