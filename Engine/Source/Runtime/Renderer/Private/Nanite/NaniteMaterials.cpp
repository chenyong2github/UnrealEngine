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

int32 GNaniteMaterialCulling = 4;
static FAutoConsoleVariableRef CVarNaniteMaterialCulling(
	TEXT("r.Nanite.MaterialCulling"),
	GNaniteMaterialCulling,
	TEXT("0: Disable culling\n")
	TEXT("1: Cull full screen passes for occluded materials\n")
	TEXT("2: Cull individual screen space tiles on 8x4 grid\n")
	TEXT("3: Cull individual screen space tiles on 64x64 grid - method 1\n")
	TEXT("4: Cull individual screen space tiles on 64x64 grid - method 2")
);

static TAutoConsoleVariable<int32> CVarParallelBasePassBuild(
	TEXT("r.Nanite.ParallelBasePassBuild"),
	1,
	TEXT(""),
	ECVF_RenderThreadSafe
);

class FNaniteMarkStencilPS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER(FNaniteMarkStencilPS);
	SHADER_USE_PARAMETER_STRUCT(FNaniteMarkStencilPS, FNaniteShader);

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
IMPLEMENT_GLOBAL_SHADER(FNaniteMarkStencilPS, "/Engine/Private/Nanite/ExportGBuffer.usf", "MarkStencilPS", SF_Pixel);

class FEmitMaterialDepthPS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER(FEmitMaterialDepthPS);
	SHADER_USE_PARAMETER_STRUCT(FEmitMaterialDepthPS, FNaniteShader);

	class FNaniteMaskDim : SHADER_PERMUTATION_BOOL("NANITE_MASK");
	using FPermutationDomain = TShaderPermutationDomain<FNaniteMaskDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FNaniteShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(uint32, DummyZero)

		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, VisibleClustersSWHW)
		SHADER_PARAMETER(FIntVector4, SOAStrides)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, ClusterPageData)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, ClusterPageHeaders)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, NaniteMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisBuffer64)

		SHADER_PARAMETER_SRV(ByteAddressBuffer, MaterialDepthTable)

		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FEmitMaterialDepthPS, "/Engine/Private/Nanite/ExportGBuffer.usf", "EmitMaterialDepthPS", SF_Pixel);

IMPLEMENT_GLOBAL_SHADER(FNaniteMaterialVS, "/Engine/Private/Nanite/ExportGBuffer.usf", "FullScreenVS", SF_Vertex);

class FEmitSceneDepthPS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER(FEmitSceneDepthPS);
	SHADER_USE_PARAMETER_STRUCT(FEmitSceneDepthPS, FNaniteShader);

	class FVelocityExportDim : SHADER_PERMUTATION_BOOL("VELOCITY_EXPORT");
	using FPermutationDomain = TShaderPermutationDomain<FVelocityExportDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FNaniteShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FPackedView>, InViews)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, VisibleClustersSWHW)
		SHADER_PARAMETER(FIntVector4, SOAStrides)
		SHADER_PARAMETER_RDG_TEXTURE( Texture2D<UlongType>,	VisBuffer64 )
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FEmitSceneDepthPS, "/Engine/Private/Nanite/ExportGBuffer.usf", "EmitSceneDepthPS", SF_Pixel);

class FEmitSceneStencilPS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER(FEmitSceneStencilPS);
	SHADER_USE_PARAMETER_STRUCT(FEmitSceneStencilPS, FNaniteShader);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FNaniteShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, VisibleClustersSWHW)
		SHADER_PARAMETER(FIntVector4, SOAStrides)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, ClusterPageData)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, ClusterPageHeaders)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, NaniteMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisBuffer64)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FEmitSceneStencilPS, "/Engine/Private/Nanite/ExportGBuffer.usf", "EmitSceneStencilPS", SF_Pixel);

class FEmitSceneDepthStencilPS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER(FEmitSceneDepthStencilPS);
	SHADER_USE_PARAMETER_STRUCT(FEmitSceneDepthStencilPS, FNaniteShader);

	class FVelocityExportDim : SHADER_PERMUTATION_BOOL("VELOCITY_EXPORT");
	using FPermutationDomain = TShaderPermutationDomain<FVelocityExportDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FNaniteShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.CompilerFlags.Add(CFLAG_ForceDXC);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FPackedView>, InViews)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, VisibleClustersSWHW)
		SHADER_PARAMETER(FIntVector4, SOAStrides)
		SHADER_PARAMETER(uint32, StencilClear)
		SHADER_PARAMETER(uint32, StencilDecal)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, ClusterPageData)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, ClusterPageHeaders)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisBuffer64)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FEmitSceneDepthStencilPS, "/Engine/Private/Nanite/ExportGBuffer.usf", "EmitSceneDepthStencilPS", SF_Pixel);

class FDepthExportCS : public FNaniteShader
{
	DECLARE_GLOBAL_SHADER(FDepthExportCS);
	SHADER_USE_PARAMETER_STRUCT(FDepthExportCS, FNaniteShader);

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
		SHADER_PARAMETER(FIntVector4, SOAStrides)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, ClusterPageData)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, ClusterPageHeaders)
		SHADER_PARAMETER(FIntVector4, DepthExportConfig)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisBuffer64)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, Velocity)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, NaniteMask)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTextureMetadata, SceneHTile)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, SceneDepth)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, SceneStencil)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTextureMetadata, MaterialHTile)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, MaterialDepth)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, MaterialDepthTable)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FDepthExportCS, "/Engine/Private/Nanite/DepthExport.usf", "DepthExport", SF_Compute);

class FClassifyMaterialsCS : public FNaniteShader
{
public:
	class FCullingMethodDim : SHADER_PERMUTATION_INT("CULLING_METHOD", 5);
	using FPermutationDomain = TShaderPermutationDomain<FCullingMethodDim>;

	DECLARE_GLOBAL_SHADER(FClassifyMaterialsCS);
	SHADER_USE_PARAMETER_STRUCT(FClassifyMaterialsCS, FNaniteShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, VisibleClustersSWHW)
		SHADER_PARAMETER(FIntVector4, SOAStrides)
		SHADER_PARAMETER(FIntVector4, ViewRect)
		SHADER_PARAMETER(FIntPoint, FetchClamp)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, ClusterPageData)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, ClusterPageHeaders)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisBuffer64)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint2>, MaterialRange)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, VisibleMaterials)
		SHADER_PARAMETER_SRV(ByteAddressBuffer, MaterialDepthTable)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		const uint32 CullingMethod = uint32(PermutationVector.Get<FCullingMethodDim>());
		if (CullingMethod == 0)
		{
			// No culling - don't bother compiling this permutation. Keeps it 1:1 with cvar
			return false;
		}

		if ((CullingMethod == 1 || CullingMethod == 2) && !FDataDrivenShaderPlatformInfo::GetSupportsWaveOperations(Parameters.Platform))
		{
			// Platform doesn't support necessary wave intrinsics for the 8x4 grid method
			return false;
		}

		return DoesPlatformSupportNanite(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FNaniteShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		FPermutationDomain PermutationVector(Parameters.PermutationId);
		const uint32 CullingMethod = uint32(PermutationVector.Get<FCullingMethodDim>());
		OutEnvironment.SetDefine(TEXT("MATERIAL_CULLING_METHOD"), CullingMethod);
	}
};
IMPLEMENT_GLOBAL_SHADER(FClassifyMaterialsCS, "/Engine/Private/Nanite/MaterialCulling.usf", "ClassifyMaterials", SF_Compute);

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
	SHADER_PARAMETER(FIntVector4,	SOAStrides)
	SHADER_PARAMETER(uint32,		MaxVisibleClusters)
	SHADER_PARAMETER(uint32,		MaxNodes)
	SHADER_PARAMETER(uint32,		RenderFlags)
	SHADER_PARAMETER(FIntPoint,		GridSize)
	
	SHADER_PARAMETER_SRV( ByteAddressBuffer, ClusterPageData )
	SHADER_PARAMETER_SRV( ByteAddressBuffer, ClusterPageHeaders )

	SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer,	VisibleClustersSWHW)

	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint2>, MaterialRange)
	SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VisibleMaterials)

	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, VisBuffer64)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<UlongType>, DbgBuffer64)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>,  DbgBuffer32)

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

	FRenderTargetBindingSlots GBufferRenderTargets;
	SceneTextures.GetGBufferRenderTargets(ERenderTargetLoadAction::ELoad, GBufferRenderTargets);

	FRDGTextureRef MaterialDepth	= RasterResults.MaterialDepth ? RasterResults.MaterialDepth : SystemTextures.Black;
	FRDGTextureRef VisBuffer64		= RasterResults.VisBuffer64   ? RasterResults.VisBuffer64   : SystemTextures.Black;
	FRDGTextureRef DbgBuffer64		= RasterResults.DbgBuffer64   ? RasterResults.DbgBuffer64   : SystemTextures.Black;
	FRDGTextureRef DbgBuffer32		= RasterResults.DbgBuffer32   ? RasterResults.DbgBuffer32   : SystemTextures.Black;

	FRDGBufferRef VisibleClustersSWHW	= RasterResults.VisibleClustersSWHW;

	if (!FDataDrivenShaderPlatformInfo::GetSupportsWaveOperations(GMaxRHIShaderPlatform) &&
		(GNaniteMaterialCulling == 1 || GNaniteMaterialCulling == 2))
	{
		// Invalid culling method, platform does not support wave operations
		// Default to 64x64 tile grid method instead.
		UE_LOG(LogNanite, Warning, TEXT("r.Nanite.MaterialCulling set to %d which requires wave-ops (not supported on this platform), switching to mode 4"), GNaniteMaterialCulling);
		GNaniteMaterialCulling = 4;
	}

	// Use local copy so we can override without modifying for all views
	int32 NaniteMaterialCulling = GNaniteMaterialCulling;
	if ((NaniteMaterialCulling == 1 || NaniteMaterialCulling == 2) && (View.ViewRect.Min.X != 0 || View.ViewRect.Min.Y != 0))
	{
		NaniteMaterialCulling = 4;

		static bool bLoggedAlready = false;
		if (!bLoggedAlready)
		{
			bLoggedAlready = true;
			UE_LOG(LogNanite, Warning, TEXT("View has non-zero viewport offset, using material culling mode 4 (overrides r.Nanite.MaterialCulling = %d)."), GNaniteMaterialCulling);
		}
	}

	const bool b32BitMaskCulling = (NaniteMaterialCulling == 1 || NaniteMaterialCulling == 2);
	const bool bTileGridCulling  = (NaniteMaterialCulling == 3 || NaniteMaterialCulling == 4);

	const FIntPoint TileGridDim = bTileGridCulling ? FMath::DivideAndRoundUp(ViewSize, { 64, 64 }) : FIntPoint(1, 1);

	FRDGBufferDesc     VisibleMaterialsDesc	= FRDGBufferDesc::CreateStructuredDesc(4, b32BitMaskCulling ? FNaniteCommandInfo::MAX_STATE_BUCKET_ID+1 : 1);
	FRDGBufferRef      VisibleMaterials		= GraphBuilder.CreateBuffer(VisibleMaterialsDesc, TEXT("Nanite.VisibleMaterials"));
	FRDGBufferUAVRef   VisibleMaterialsUAV	= GraphBuilder.CreateUAV(VisibleMaterials);
	FRDGTextureDesc    MaterialRangeDesc	= FRDGTextureDesc::Create2D(TileGridDim, PF_R32G32_UINT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV);
	FRDGTextureRef     MaterialRange		= GraphBuilder.CreateTexture(MaterialRangeDesc, TEXT("Nanite.MaterialRange"));
	FRDGTextureUAVRef  MaterialRangeUAV		= GraphBuilder.CreateUAV(MaterialRange);
	FRDGTextureSRVDesc MaterialRangeSRVDesc	= FRDGTextureSRVDesc::Create(MaterialRange);
	FRDGTextureSRVRef  MaterialRangeSRV		= GraphBuilder.CreateSRV(MaterialRangeSRVDesc);

	FRDGBufferRef MultiViewIndices = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), 1), TEXT("Nanite.DummyMultiViewIndices"));
	FRDGBufferRef MultiViewRectScaleOffsets = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4), 1), TEXT("Nanite.DummyMultiViewRectScaleOffsets"));
	FRDGBufferRef ViewsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4), 1), TEXT("Nanite.PackedViews"));

	AddClearUAVPass(GraphBuilder, VisibleMaterialsUAV, 0);
	AddClearUAVPass(GraphBuilder, MaterialRangeUAV, { 0u, 1u, 0u, 0u });
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(MultiViewIndices), 0);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(MultiViewRectScaleOffsets), 0);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(ViewsBuffer), 0);

	// Classify materials for tile culling
	// TODO: Run velocity export in here instead of depth pre-pass?
	if (b32BitMaskCulling || bTileGridCulling)
	{
		FClassifyMaterialsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FClassifyMaterialsCS::FParameters>();
		PassParameters->View					= View.ViewUniformBuffer;
		PassParameters->VisibleClustersSWHW		= GraphBuilder.CreateSRV(VisibleClustersSWHW);
		PassParameters->SOAStrides				= RasterResults.SOAStrides;
		PassParameters->ClusterPageData			= Nanite::GStreamingManager.GetClusterPageDataSRV();
		PassParameters->ClusterPageHeaders		= Nanite::GStreamingManager.GetClusterPageHeadersSRV();
		PassParameters->VisBuffer64				= VisBuffer64;
		PassParameters->MaterialDepthTable		= Scene.NaniteMaterials[ENaniteMeshPass::BasePass].GetDepthTableSRV();

		uint32 DispatchGroupSize = 0;

		PassParameters->ViewRect = FIntVector4(View.ViewRect.Min.X, View.ViewRect.Min.Y, View.ViewRect.Max.X, View.ViewRect.Max.Y);
		if (b32BitMaskCulling)
		{
			// TODO: Don't currently support offset views.
			checkf(View.ViewRect.Min.X == 0 && View.ViewRect.Min.Y == 0, TEXT("Viewport offset support is not implemented."));
			DispatchGroupSize = 8;
			PassParameters->VisibleMaterials = VisibleMaterialsUAV;

		}
		else if (bTileGridCulling)
		{
			DispatchGroupSize = 64;
			PassParameters->FetchClamp = View.ViewRect.Max - 1;
			PassParameters->MaterialRange = MaterialRangeUAV;
		}

		const FIntVector DispatchDim = FComputeShaderUtils::GetGroupCount(View.ViewRect.Max - View.ViewRect.Min, DispatchGroupSize);

		FClassifyMaterialsCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FClassifyMaterialsCS::FCullingMethodDim>(NaniteMaterialCulling);
		auto ComputeShader = View.ShaderMap->GetShader<FClassifyMaterialsCS>(PermutationVector.ToDimensionValueId());

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("Classify Materials"),
			ComputeShader,
			PassParameters,
			DispatchDim
		);
	}

	// Emit GBuffer Values
	{
		FNaniteEmitGBufferParameters* PassParameters = GraphBuilder.AllocParameters<FNaniteEmitGBufferParameters>();

		PassParameters->SOAStrides	= RasterResults.SOAStrides;
		PassParameters->MaxVisibleClusters	= RasterResults.MaxVisibleClusters;
		PassParameters->MaxNodes	= RasterResults.MaxNodes;
		PassParameters->RenderFlags	= RasterResults.RenderFlags;
			
		PassParameters->ClusterPageData		= Nanite::GStreamingManager.GetClusterPageDataSRV(); 
		PassParameters->ClusterPageHeaders	= Nanite::GStreamingManager.GetClusterPageHeadersSRV();

		PassParameters->VisibleClustersSWHW = GraphBuilder.CreateSRV(VisibleClustersSWHW);

		PassParameters->MaterialRange = MaterialRange;
		PassParameters->VisibleMaterials = GraphBuilder.CreateSRV(VisibleMaterials, PF_R32_UINT);

		PassParameters->MultiViewEnabled = 0;
		PassParameters->MultiViewIndices = GraphBuilder.CreateSRV(MultiViewIndices);
		PassParameters->MultiViewRectScaleOffsets = GraphBuilder.CreateSRV(MultiViewRectScaleOffsets);
		PassParameters->InViews = GraphBuilder.CreateSRV(ViewsBuffer);

		PassParameters->VisBuffer64 = VisBuffer64;
		PassParameters->DbgBuffer64 = DbgBuffer64;
		PassParameters->DbgBuffer32 = DbgBuffer32;
		PassParameters->RenderTargets = GBufferRenderTargets;

		PassParameters->View = View.ViewUniformBuffer; // To get VTFeedbackBuffer
		PassParameters->BasePass = CreateOpaqueBasePassUniformBuffer(GraphBuilder, View, 0, {}, DBufferTextures, nullptr);

		switch (NaniteMaterialCulling)
		{
		// Rendering 32 tiles in a 8x4 grid
		// 32bits, 1 bit per tile
		case 1:
		case 2:
			PassParameters->GridSize.X = 8;
			PassParameters->GridSize.Y = 4;
			break;

		// Rendering grid of 64x64 pixel tiles
		case 3:
		case 4:
			PassParameters->GridSize = FMath::DivideAndRoundUp(View.ViewRect.Max - View.ViewRect.Min, { 64, 64 });
			break;

		// Rendering a full screen quad
		default:
			PassParameters->GridSize.X = 1;
			PassParameters->GridSize.Y = 1;
			break;
		}

		const FExclusiveDepthStencil MaterialDepthStencil = UseComputeDepthExport()
			? FExclusiveDepthStencil::DepthWrite_StencilNop
			: FExclusiveDepthStencil::DepthWrite_StencilWrite;

		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
			MaterialDepth,
			ERenderTargetLoadAction::ELoad,
			ERenderTargetLoadAction::ELoad,
			MaterialDepthStencil
		);

		TShaderMapRef<FNaniteMaterialVS> NaniteVertexShader(View.ShaderMap);
				
		ERDGPassFlags RDGPassFlags = ERDGPassFlags::Raster;

		// Skip render pass when parallel because that's taken care of by the FRDGParallelCommandListSet
		bool bParallelBasePassBuild = GRHICommandList.UseParallelAlgorithms() && CVarParallelBasePassBuild.GetValueOnRenderThread() != 0;
		if (bParallelBasePassBuild)
		{
			RDGPassFlags |= ERDGPassFlags::SkipRenderPass;
		}

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("Emit GBuffer"),
			PassParameters,
			RDGPassFlags,
			[PassParameters, &SceneRenderer, &Scene, NaniteVertexShader, &View, &MaterialPassCommands, bParallelBasePassBuild, NaniteMaterialCulling](FRHICommandListImmediate& RHICmdListImmediate)
		{
			RHICmdListImmediate.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

			FNaniteUniformParameters UniformParams;
			UniformParams.SOAStrides = PassParameters->SOAStrides;
			UniformParams.MaxVisibleClusters= PassParameters->MaxVisibleClusters;
			UniformParams.MaxNodes = PassParameters->MaxNodes;
			UniformParams.RenderFlags = PassParameters->RenderFlags;

			UniformParams.MaterialConfig.X = NaniteMaterialCulling;
			UniformParams.MaterialConfig.Y = PassParameters->GridSize.X;
			UniformParams.MaterialConfig.Z = PassParameters->GridSize.Y;
			UniformParams.MaterialConfig.W = 0;

			UniformParams.RectScaleOffset = FVector4(1.0f, 1.0f, 0.0f, 0.0f); // Render a rect that covers the entire screen

			if (NaniteMaterialCulling == 3 || NaniteMaterialCulling == 4)
			{
				FIntPoint ScaledSize = PassParameters->GridSize * 64;
				UniformParams.RectScaleOffset.X = float(ScaledSize.X) / float(View.ViewRect.Max.X - View.ViewRect.Min.X);
				UniformParams.RectScaleOffset.Y = float(ScaledSize.Y) / float(View.ViewRect.Max.Y - View.ViewRect.Min.Y);
			}

			UniformParams.ClusterPageData = PassParameters->ClusterPageData;
			UniformParams.ClusterPageHeaders = PassParameters->ClusterPageHeaders;
			UniformParams.VisibleClustersSWHW = PassParameters->VisibleClustersSWHW->GetRHI();

			UniformParams.MaterialRange = PassParameters->MaterialRange->GetRHI();
			UniformParams.VisibleMaterials = PassParameters->VisibleMaterials->GetRHI();

			UniformParams.MultiViewEnabled = PassParameters->MultiViewEnabled;
			UniformParams.MultiViewIndices = PassParameters->MultiViewIndices->GetRHI();
			UniformParams.MultiViewRectScaleOffsets = PassParameters->MultiViewRectScaleOffsets->GetRHI();
			UniformParams.InViews = PassParameters->InViews->GetRHI();

			UniformParams.VisBuffer64 = PassParameters->VisBuffer64->GetRHI();
			UniformParams.DbgBuffer64 = PassParameters->DbgBuffer64->GetRHI();
			UniformParams.DbgBuffer32 = PassParameters->DbgBuffer32->GetRHI();
			const_cast<FScene&>(Scene).UniformBuffers.NaniteUniformBuffer.UpdateUniformBufferImmediate(UniformParams);

			const uint32 TileCount = UniformParams.MaterialConfig.Y * UniformParams.MaterialConfig.Z; // (W * H)

			DrawNaniteMaterialPasses(
				SceneRenderer,
				Scene,
				View,
				TileCount,
				bParallelBasePassBuild,
				FParallelCommandListBindings(PassParameters),
				NaniteVertexShader,
				RHICmdListImmediate,
				MaterialPassCommands
			);
		});
	}
}

void EmitDepthTargets(
	FRDGBuilder& GraphBuilder,
	const FScene& Scene,
	const FViewInfo& View,
	const FIntVector4& SOAStrides,
	FRDGBufferRef VisibleClustersSWHW,
	FRDGBufferRef ViewsBuffer,
	FRDGTextureRef SceneDepth,
	FRDGTextureRef VisBuffer64,
	FRDGTextureRef VelocityBuffer,
	FRDGTextureRef& OutMaterialDepth,
	FRDGTextureRef& OutNaniteMask,
	bool bPrePass,
	bool bStencilMask
)
{
	LLM_SCOPE_BYTAG(Nanite);
	RDG_EVENT_SCOPE(GraphBuilder, "Nanite::EmitDepthTargets");
	RDG_GPU_STAT_SCOPE(GraphBuilder, NaniteDepth);

	const EShaderPlatform ShaderPlatform = View.GetShaderPlatform();
	const FIntPoint SceneTexturesExtent = GetSceneTextureExtent();	
	const FClearValueBinding DefaultDepthStencil = GetSceneDepthClearValue();

	float DefaultDepth = 0.0f;
	uint32 DefaultStencil = 0;
	DefaultDepthStencil.GetDepthStencil(DefaultDepth, DefaultStencil);

	const uint32 StencilDecalMask = GET_STENCIL_BIT_MASK(RECEIVE_DECAL, 1);

	const bool bEmitVelocity = VelocityBuffer != nullptr;
	const bool bClearVelocity = bEmitVelocity && !HasBeenProduced(VelocityBuffer);

	// Nanite mask (TODO: unpacked right now, 7bits wasted per pixel).
	FRDGTextureDesc NaniteMaskDesc = FRDGTextureDesc::Create2D(
		SceneTexturesExtent,
		PF_R8_UINT,
		FClearValueBinding::Transparent,
		TexCreate_RenderTargetable | TexCreate_ShaderResource | TexCreate_UAV);

	// TODO: Can be 16bit UNORM (PF_ShadowDepth) (32bit float w/ 8bit stencil is a waste of bandwidth and memory)
	FRDGTextureDesc MaterialDepthDesc = FRDGTextureDesc::Create2D(
		SceneTexturesExtent,
		PF_DepthStencil,
		DefaultDepthStencil,
		TexCreate_DepthStencilTargetable | TexCreate_ShaderResource | TexCreate_InputAttachmentRead | (UseComputeDepthExport() ? TexCreate_UAV : TexCreate_None));

	FRDGTextureRef NaniteMask		= GraphBuilder.CreateTexture(NaniteMaskDesc, TEXT("Nanite.Mask"));
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

		FRDGTextureUAVRef SceneDepthUAV		= GraphBuilder.CreateUAV(FRDGTextureUAVDesc::CreateForMetaData(SceneDepth, ERDGTextureMetaDataAccess::CompressedSurface));
		FRDGTextureUAVRef SceneStencilUAV	= GraphBuilder.CreateUAV(FRDGTextureUAVDesc::CreateForMetaData(SceneDepth, ERDGTextureMetaDataAccess::Stencil));
		FRDGTextureUAVRef SceneHTileUAV		= GraphBuilder.CreateUAV(FRDGTextureUAVDesc::CreateForMetaData(SceneDepth, ERDGTextureMetaDataAccess::HTile));
		FRDGTextureUAVRef MaterialDepthUAV	= GraphBuilder.CreateUAV(FRDGTextureUAVDesc::CreateForMetaData(MaterialDepth, ERDGTextureMetaDataAccess::CompressedSurface));
		FRDGTextureUAVRef MaterialHTileUAV	= GraphBuilder.CreateUAV(FRDGTextureUAVDesc::CreateForMetaData(MaterialDepth, ERDGTextureMetaDataAccess::HTile));
		FRDGTextureUAVRef VelocityUAV		= bEmitVelocity ? GraphBuilder.CreateUAV(VelocityBuffer) : nullptr;
		FRDGTextureUAVRef NaniteMaskUAV		= GraphBuilder.CreateUAV(NaniteMask);

		FDepthExportCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDepthExportCS::FParameters>();

		PassParameters->View					= View.ViewUniformBuffer;
		PassParameters->InViews					= GraphBuilder.CreateSRV(ViewsBuffer);
		PassParameters->VisibleClustersSWHW		= GraphBuilder.CreateSRV(VisibleClustersSWHW);
		PassParameters->SOAStrides				= SOAStrides;
		PassParameters->ClusterPageData			= Nanite::GStreamingManager.GetClusterPageDataSRV();
		PassParameters->ClusterPageHeaders		= Nanite::GStreamingManager.GetClusterPageHeadersSRV();
		PassParameters->DepthExportConfig		= FIntVector4(PlatformConfig, SceneTexturesExtent.X, StencilDecalMask, 0);
		PassParameters->VisBuffer64				= VisBuffer64;
		PassParameters->Velocity				= VelocityUAV;
		PassParameters->NaniteMask				= NaniteMaskUAV;
		PassParameters->SceneHTile				= SceneHTileUAV;
		PassParameters->SceneDepth				= SceneDepthUAV;
		PassParameters->SceneStencil			= SceneStencilUAV;
		PassParameters->MaterialHTile			= MaterialHTileUAV;
		PassParameters->MaterialDepth			= MaterialDepthUAV;
		PassParameters->MaterialDepthTable		= Scene.NaniteMaterials[ENaniteMeshPass::BasePass].GetDepthTableSRV();

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
		AddClearRenderTargetPass(GraphBuilder, NaniteMask);
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
			PassParameters->SOAStrides					= SOAStrides;
			PassParameters->StencilClear				= DefaultStencil;
			PassParameters->StencilDecal				= StencilDecalMask;
			PassParameters->ClusterPageData				= Nanite::GStreamingManager.GetClusterPageDataSRV();
			PassParameters->ClusterPageHeaders			= Nanite::GStreamingManager.GetClusterPageHeadersSRV();
			PassParameters->VisBuffer64					= VisBuffer64;
			PassParameters->RenderTargets[0]			= FRenderTargetBinding(NaniteMask, ERenderTargetLoadAction::ELoad);
			PassParameters->RenderTargets[1]			= bEmitVelocity ? FRenderTargetBinding(VelocityBuffer, ERenderTargetLoadAction::ELoad) : FRenderTargetBinding();
			PassParameters->RenderTargets.DepthStencil	= FDepthStencilBinding(SceneDepth, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilWrite);

			FPixelShaderUtils::AddFullscreenPass(
				GraphBuilder,
				View.ShaderMap,
				RDG_EVENT_NAME("Emit Scene Depth/Stencil/Mask/Velocity"),
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
				auto  PixelShader = View.ShaderMap->GetShader<FEmitSceneDepthPS>(PermutationVectorPS);
				
				auto* PassParameters = GraphBuilder.AllocParameters<FEmitSceneDepthPS::FParameters>();

				PassParameters->View						= View.ViewUniformBuffer;
				PassParameters->InViews						= GraphBuilder.CreateSRV(ViewsBuffer);
				PassParameters->VisibleClustersSWHW			= GraphBuilder.CreateSRV(VisibleClustersSWHW);
				PassParameters->SOAStrides					= SOAStrides;
				PassParameters->VisBuffer64					= VisBuffer64;
				PassParameters->RenderTargets[0]			= FRenderTargetBinding(NaniteMask,	ERenderTargetLoadAction::ELoad);
				PassParameters->RenderTargets[1]			= bEmitVelocity ? FRenderTargetBinding(VelocityBuffer, ERenderTargetLoadAction::ELoad) : FRenderTargetBinding();
				PassParameters->RenderTargets.DepthStencil	= FDepthStencilBinding(SceneDepth, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilWrite);

				FPixelShaderUtils::AddFullscreenPass(
					GraphBuilder,
					View.ShaderMap,
					RDG_EVENT_NAME("Emit Scene Depth/Mask/Velocity"),
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
				PassParameters->SOAStrides					= SOAStrides;
				PassParameters->ClusterPageData				= Nanite::GStreamingManager.GetClusterPageDataSRV();
				PassParameters->ClusterPageHeaders			= Nanite::GStreamingManager.GetClusterPageHeadersSRV();
				PassParameters->NaniteMask					= NaniteMask;
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
			PassParameters->ClusterPageData				= Nanite::GStreamingManager.GetClusterPageDataSRV();
			PassParameters->ClusterPageHeaders			= Nanite::GStreamingManager.GetClusterPageHeadersSRV();
			PassParameters->MaterialDepthTable			= Scene.NaniteMaterials[ENaniteMeshPass::BasePass].GetDepthTableSRV();
			PassParameters->SOAStrides					= SOAStrides;
			PassParameters->View						= View.ViewUniformBuffer;
			PassParameters->NaniteMask					= NaniteMask;
			PassParameters->VisBuffer64					= VisBuffer64;
			PassParameters->VisibleClustersSWHW			= GraphBuilder.CreateSRV(VisibleClustersSWHW);
			PassParameters->RenderTargets.DepthStencil	= FDepthStencilBinding(
				MaterialDepth,
				ERenderTargetLoadAction::EClear,
				FExclusiveDepthStencil::DepthWrite_StencilWrite
			);

			FEmitMaterialDepthPS::FPermutationDomain PermutationVectorPS;
			PermutationVectorPS.Set<FEmitMaterialDepthPS::FNaniteMaskDim>(true /* using Nanite mask */);
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

	OutNaniteMask = NaniteMask;
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
	int32 CommandStateBucketId = INDEX_NONE;
	uint32 ViewIndexBufferOffset = 0;
	TArray<uint16, TInlineAllocator<256>> ViewIndices;

	inline float GetMaterialDepth() const
	{
		return FNaniteCommandInfo::GetDepthId(CommandStateBucketId);
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

	// Material range placeholder (not used by Lumen, but must still be bound)
	FRDGTextureDesc MaterialRangeDesc = FRDGTextureDesc::Create2D(FIntPoint(1, 1), PF_R32G32_UINT, FClearValueBinding::Black, TexCreate_UAV | TexCreate_ShaderResource);
	FRDGTextureRef  MaterialRange = GraphBuilder.CreateTexture(MaterialRangeDesc, TEXT("Nanite.MaterialRange"));

	const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);

	// Visible material mask buffer (currently not used by Lumen, but still must be bound)
	FRDGBufferDesc   VisibleMaterialsDesc = FRDGBufferDesc::CreateStructuredDesc(4, 1);
	FRDGBufferRef    VisibleMaterials     = GraphBuilder.CreateBuffer(VisibleMaterialsDesc, TEXT("Nanite.VisibleMaterials"));
	FRDGBufferUAVRef VisibleMaterialsUAV  = GraphBuilder.CreateUAV(VisibleMaterials);
	AddClearUAVPass(GraphBuilder, VisibleMaterialsUAV, 0);
	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(MaterialRange), FUintVector4(0, 0, 0, 0));

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
		PassParameters->PS.SOAStrides = CullingContext.SOAStrides;
		PassParameters->PS.ClusterPageData = GStreamingManager.GetClusterPageDataSRV();
		PassParameters->PS.ClusterPageHeaders = GStreamingManager.GetClusterPageHeadersSRV();

		PassParameters->PS.VisBuffer64 = RasterContext.VisBuffer64;

		PassParameters->PS.MaterialDepthTable = Scene.NaniteMaterials[ENaniteMeshPass::LumenCardCapture].GetDepthTableSRV();

		PassParameters->PS.RenderTargets.DepthStencil = FDepthStencilBinding(
			DepthAtlasTexture,
			ERenderTargetLoadAction::ELoad,
			ERenderTargetLoadAction::ELoad,
			FExclusiveDepthStencil::DepthWrite_StencilRead
		);

		FEmitMaterialDepthPS::FPermutationDomain PermutationVectorPS;
		PermutationVectorPS.Set<FEmitMaterialDepthPS::FNaniteMaskDim>(false /* not using Nanite mask */);
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
		int32 NumMaterialQuads = 0;

		TArray<FLumenMeshCaptureMaterialPass, SceneRenderingAllocator> MaterialPasses;
		MaterialPasses.Reserve(CardPagesToRender.Num());

		// Build list of unique materials
		{
			Experimental::TRobinHoodHashSet<FLumenMeshCaptureMaterialPassIndex> MaterialPassSet;

			for (int32 CardPageIndex = 0; CardPageIndex < CardPagesToRender.Num(); ++CardPageIndex)
			{
				const FCardPageRenderData& CardPageRenderData = CardPagesToRender[CardPageIndex];

				for (const FNaniteCommandInfo& CommandInfo : CardPageRenderData.NaniteCommandInfos)
				{
					const FLumenMeshCaptureMaterialPassIndex& PassIndex = *MaterialPassSet.FindOrAdd(FLumenMeshCaptureMaterialPassIndex(MaterialPasses.Num(), CommandInfo.GetStateBucketId()));

					if (PassIndex.Index >= MaterialPasses.Num())
					{
						FLumenMeshCaptureMaterialPass MaterialPass;
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

		TArray<FVector4, SceneRenderingAllocator> ViewRectScaleOffsets;
		ViewRectScaleOffsets.Reserve(CardPagesToRender.Num());

		TArray<Nanite::FPackedView, SceneRenderingAllocator> PackedViews;
		PackedViews.Reserve(CardPagesToRender.Num());

		const FVector2D ViewportSizeF = FVector2D(float(ViewportSize.X), float(ViewportSize.Y));

		for (const FCardPageRenderData& CardPageRenderData : CardPagesToRender)
		{
			const FVector2D CardViewportSize = FVector2D(float(CardPageRenderData.CardCaptureAtlasRect.Width()), float(CardPageRenderData.CardCaptureAtlasRect.Height()));
			const FVector2D RectOffset = FVector2D(float(CardPageRenderData.CardCaptureAtlasRect.Min.X), float(CardPageRenderData.CardCaptureAtlasRect.Min.Y)) / ViewportSizeF;
			const FVector2D RectScale = CardViewportSize / ViewportSizeF;

			ViewRectScaleOffsets.Add(FVector4(RectScale, RectOffset));

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

		FNaniteEmitGBufferParameters* PassParameters = GraphBuilder.AllocParameters<FNaniteEmitGBufferParameters>();

		PassParameters->SOAStrides = CullingContext.SOAStrides;
		PassParameters->MaxVisibleClusters = Nanite::FGlobalResources::GetMaxVisibleClusters();
		PassParameters->MaxNodes = Nanite::FGlobalResources::GetMaxNodes();
		PassParameters->RenderFlags = CullingContext.RenderFlags;

		PassParameters->ClusterPageData = GStreamingManager.GetClusterPageDataSRV();
		PassParameters->ClusterPageHeaders = GStreamingManager.GetClusterPageHeadersSRV();

		PassParameters->VisibleClustersSWHW = GraphBuilder.CreateSRV(CullingContext.VisibleClustersSWHW);

		PassParameters->MaterialRange = MaterialRange;
		PassParameters->GridSize = { 1u, 1u };

		PassParameters->VisibleMaterials = GraphBuilder.CreateSRV(VisibleMaterials, PF_R32_UINT);
			
		PassParameters->MultiViewEnabled = 1;
		PassParameters->MultiViewIndices = GraphBuilder.CreateSRV(ViewIndexBuffer);
		PassParameters->MultiViewRectScaleOffsets = GraphBuilder.CreateSRV(ViewRectScaleOffsetBuffer);
		PassParameters->InViews = GraphBuilder.CreateSRV(PackedViewBuffer);

		PassParameters->VisBuffer64 = RasterContext.VisBuffer64;
		PassParameters->DbgBuffer64 = SystemTextures.Black;
		PassParameters->DbgBuffer32 = SystemTextures.Black;

		PassParameters->RenderTargets[0] = FRenderTargetBinding(AlbedoAtlasTexture, ERenderTargetLoadAction::ELoad);
		PassParameters->RenderTargets[1] = FRenderTargetBinding(NormalAtlasTexture, ERenderTargetLoadAction::ELoad);
		PassParameters->RenderTargets[2] = FRenderTargetBinding(EmissiveAtlasTexture, ERenderTargetLoadAction::ELoad);

		PassParameters->View = Scene.UniformBuffers.LumenCardCaptureViewUniformBuffer;
		PassParameters->CardPass = GraphBuilder.CreateUniformBuffer(PassUniformParameters);

		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
			DepthAtlasTexture,
			ERenderTargetLoadAction::ELoad,
			ERenderTargetLoadAction::ELoad,
			FExclusiveDepthStencil::DepthWrite_StencilRead
		);

		TShaderMapRef<FNaniteMaterialVS> NaniteVertexShader(SharedView->ShaderMap);

		GraphBuilder.AddPass(
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

				FirstCardPage.PatchView(RHICmdList, &Scene, SharedView);
				Scene.UniformBuffers.LumenCardCaptureViewUniformBuffer.UpdateUniformBufferImmediate(*SharedView->CachedViewUniformShaderParameters);

				FNaniteUniformParameters UniformParams;
				UniformParams.SOAStrides = PassParameters->SOAStrides;
				UniformParams.MaxVisibleClusters = PassParameters->MaxVisibleClusters;
				UniformParams.MaxNodes = PassParameters->MaxNodes;
				UniformParams.RenderFlags = PassParameters->RenderFlags;
				UniformParams.MaterialConfig = FIntVector4(0, 1, 1, 0); // Tile based material culling is not required for Lumen, as each card is rendered as a small rect
				UniformParams.RectScaleOffset = FVector4(1.0f, 1.0f, 0.0f, 0.0f); // This will be overridden in vertex shader

				UniformParams.ClusterPageData = PassParameters->ClusterPageData;
				UniformParams.ClusterPageHeaders = PassParameters->ClusterPageHeaders;

				UniformParams.VisibleClustersSWHW = PassParameters->VisibleClustersSWHW->GetRHI();

				UniformParams.MaterialRange = PassParameters->MaterialRange->GetRHI();
				UniformParams.VisibleMaterials = PassParameters->VisibleMaterials->GetRHI();

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

					SubmitNaniteMaterialPassCommand(
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
			});
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
	DepthTableUploadBuffer.Release();
	DepthTableDataBuffer.Release();
	HitProxyTableUploadBuffer.Release();
	HitProxyTableDataBuffer.Release();
}

FNaniteCommandInfo FNaniteMaterialCommands::Register(FMeshDrawCommand& Command)
{
	const FCommandHash CommandHash = ComputeCommandHash(Command);

	FCommandId CommandId;
	{
		FNaniteMaterialCommandsLock Lock(*this, SLT_ReadOnly);

		CommandId = FindIdByHash(CommandHash, Command);
		if (!CommandId.IsValid())
		{
			Lock.AcquireWriteAccess();
			CommandId = FindOrAddIdByHash(CommandHash, Command);

		#if MESH_DRAW_COMMAND_DEBUG_DATA
			FMeshDrawCommandCount& DrawCount = GetPayload(CommandId);
			if (DrawCount.Num == 0)
			{
				// When using State Buckets multiple PrimitiveSceneProxies use the same MeshDrawCommand, so The PrimitiveSceneProxy pointer can't be stored.
				Command.ClearDebugPrimitiveSceneProxy();
			}
		#endif
		}

		FMeshDrawCommandCount& DrawCount = GetPayload(CommandId);
		++DrawCount.Num;
	}

	FNaniteCommandInfo CommandInfo;
	CommandInfo.SetStateBucketId(CommandId.GetIndex());
	return CommandInfo;
}

void FNaniteMaterialCommands::Unregister(const FNaniteCommandInfo& CommandInfo)
{
	if (CommandInfo.GetStateBucketId() == INDEX_NONE)
	{
		return;
	}

	FGraphicsMinimalPipelineStateId CachedPipelineId;
	{
		FNaniteMaterialCommandsLock Lock(*this, SLT_ReadOnly);

		const FMeshDrawCommand& MeshDrawCommand = GetCommand(CommandInfo.GetStateBucketId());
		CachedPipelineId = MeshDrawCommand.CachedPipelineId;

		FMeshDrawCommandCount& StateBucketCount = GetPayload(CommandInfo.GetStateBucketId());
		check(StateBucketCount.Num > 0);

		--StateBucketCount.Num;
		if (StateBucketCount.Num == 0)
		{
			Lock.AcquireWriteAccess();
			if (StateBucketCount.Num == 0)
			{
				RemoveById(CommandInfo.GetStateBucketId());
			}
		}
	}

	FGraphicsMinimalPipelineStateId::RemovePersistentId(CachedPipelineId);
}

void FNaniteMaterialCommands::UpdateBufferState(FRDGBuilder& GraphBuilder, uint32 NumPrimitives)
{
	checkSlow(DoesPlatformSupportNanite(GMaxRHIShaderPlatform));

	LLM_SCOPE_BYTAG(Nanite);

	check(NumPrimitiveUpdates == 0);
	check(NumDepthTableUpdates == 0);
#if WITH_EDITOR
	check(NumHitProxyTableUpdates == 0);
#endif

	TArray<FRHITransitionInfo, TInlineAllocator<2>> UAVs;

	const uint32 SizeReserve = FMath::RoundUpToPowerOfTwo(FMath::Max(NumPrimitives * MaxMaterials, 256u));

	if (ResizeResourceIfNeeded(GraphBuilder, DepthTableDataBuffer, SizeReserve * sizeof(uint32), TEXT("Nanite.DepthTableDataBuffer")))
	{
		UAVs.Add(FRHITransitionInfo(DepthTableDataBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVMask));
	}
#if WITH_EDITOR
	if (ResizeResourceIfNeeded(GraphBuilder, HitProxyTableDataBuffer, SizeReserve * sizeof(uint32), TEXT("Nanite.HitProxyTableDataBuffer")))
	{
		UAVs.Add(FRHITransitionInfo(HitProxyTableDataBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVMask));
	}
#endif // WITH_EDITOR

	GraphBuilder.AddPass(RDG_EVENT_NAME("NaniteMaterialTables.UpdateBufferState-Transition"), ERDGPassFlags::None,
		[LocalUAVs = MoveTemp(UAVs)](FRHICommandListImmediate& RHICmdList)
	{
		RHICmdList.Transition(LocalUAVs);
	});
}

void FNaniteMaterialCommands::Begin(FRHICommandListImmediate& RHICmdList, uint32 NumPrimitives, uint32 InNumPrimitiveUpdates)
{
	checkSlow(DoesPlatformSupportNanite(GMaxRHIShaderPlatform));

	LLM_SCOPE_BYTAG(Nanite);

	check(NumPrimitiveUpdates == 0);
	check(NumDepthTableUpdates == 0);
	const uint32 SizeReserve = FMath::RoundUpToPowerOfTwo(FMath::Max(NumPrimitives * MaxMaterials, 256u));
#if WITH_EDITOR
	check(NumHitProxyTableUpdates == 0);

	check(HitProxyTableDataBuffer.NumBytes == SizeReserve * sizeof(uint32));
#endif
	check(DepthTableDataBuffer.NumBytes == SizeReserve * sizeof(uint32));

	NumPrimitiveUpdates = InNumPrimitiveUpdates;
	if (NumPrimitiveUpdates > 0)
	{
		DepthTableUploadBuffer.Init(NumPrimitiveUpdates * MaxMaterials, sizeof(uint32), false, TEXT("Nanite.DepthTableUploadBuffer"));
#if WITH_EDITOR
		HitProxyTableUploadBuffer.Init(NumPrimitiveUpdates * MaxMaterials, sizeof(uint32), false, TEXT("Nanite.HitProxyTableUploadBuffer"));
#endif
	}
}

void* FNaniteMaterialCommands::GetDepthTablePtr(uint32 PrimitiveIndex, uint32 EntryCount)
{
	++NumDepthTableUpdates;
	const uint32 BaseIndex = PrimitiveIndex * MaxMaterials;
	return DepthTableUploadBuffer.Add_GetRef(BaseIndex, EntryCount);
}

#if WITH_EDITOR
void* FNaniteMaterialCommands::GetHitProxyTablePtr(uint32 PrimitiveIndex, uint32 EntryCount)
{
	++NumHitProxyTableUpdates;
	const uint32 BaseIndex = PrimitiveIndex * MaxMaterials;
	return HitProxyTableUploadBuffer.Add_GetRef(BaseIndex, EntryCount);
}
#endif

void FNaniteMaterialCommands::Finish(FRHICommandListImmediate& RHICmdList)
{
	checkSlow(DoesPlatformSupportNanite(GMaxRHIShaderPlatform));

	LLM_SCOPE_BYTAG(Nanite);

#if WITH_EDITOR
	check(NumHitProxyTableUpdates <= NumPrimitiveUpdates);
#endif
	check(NumDepthTableUpdates <= NumPrimitiveUpdates);
	if (NumPrimitiveUpdates == 0)
	{
		return;
	}

	SCOPED_DRAW_EVENTF(RHICmdList, UpdateMaterialTables, TEXT("UpdateMaterialTables PrimitivesToUpdate = %u"), NumPrimitiveUpdates);

	TArray<FRHITransitionInfo, TInlineAllocator<2>> UploadUAVs;
	UploadUAVs.Add(FRHITransitionInfo(DepthTableDataBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
#if WITH_EDITOR
	UploadUAVs.Add(FRHITransitionInfo(HitProxyTableDataBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
#endif

	RHICmdList.Transition(UploadUAVs);

	DepthTableUploadBuffer.ResourceUploadTo(RHICmdList, DepthTableDataBuffer, false);
#if WITH_EDITOR
	HitProxyTableUploadBuffer.ResourceUploadTo(RHICmdList, HitProxyTableDataBuffer, false);
#endif

	for (FRHITransitionInfo& Info : UploadUAVs)
	{
		Info.AccessBefore = Info.AccessAfter;
		Info.AccessAfter = ERHIAccess::SRVMask;
	}

	RHICmdList.Transition(UploadUAVs);

	NumDepthTableUpdates = 0;
#if WITH_EDITOR
	NumHitProxyTableUpdates = 0;
#endif
	NumPrimitiveUpdates = 0;
}
