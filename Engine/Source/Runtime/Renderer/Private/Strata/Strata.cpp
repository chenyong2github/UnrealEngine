// Copyright Epic Games, Inc. All Rights Reserved.

#include "Strata.h"
#include "HAL/IConsoleManager.h"
#include "PixelShaderUtils.h"
#include "SceneView.h"
#include "ScenePrivate.h"
#include "SceneRendering.h"
#include "RendererInterface.h"
#include "UniformBuffer.h"
#include "SceneTextureParameters.h"
#include "StrataDefinitions.h"
#include "ScreenPass.h"

//PRAGMA_DISABLE_OPTIMIZATION

// The project setting for Strata
static TAutoConsoleVariable<int32> CVarStrata(
	TEXT("r.Strata"),
	0,
	TEXT("Enable Strata materials (Beta)."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarStrataBackCompatibility(
	TEXT("r.StrataBackCompatibility"),
	0,
	TEXT("Disables Strata multiple scattering and replaces Chan diffuse by Lambert."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarStrataBytePerPixel(
	TEXT("r.Strata.BytesPerPixel"),
	80,
	TEXT("Strata allocated byte per pixel to store materials data. Higher value means more complex material can be represented."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarStrataClassification(
	TEXT("r.Strata.Classification"),
	1,
	TEXT("Enable strata classification to speed up lighting pass."),
	ECVF_RenderThreadSafe); 

static TAutoConsoleVariable<int32> CVarStrataClassificationDebug(
	TEXT("r.Strata.Classification.Debug"),
	0,
	TEXT("Enable strata classification visualization: 1 shows simple material tiles in green and complex material tiles in red."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarStrataClassificationPassesReadingStrataAreTiled(
	TEXT("r.Strata.Classification.PassesReadingStrataAreTiled"),
	1,
	TEXT("Enable the tiling of passes reading strata material (when possible) instead of doing multiple full screen passes testing stencil."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarStrataRoughDiffuse(
	TEXT("r.Strata.RoughDiffuse"),
	1,
	TEXT("Enable Strata rough diffuse model (works only if r.Material.RoughDiffuse is enabled in the project settings). Togglable at runtime"),
	ECVF_RenderThreadSafe);

// Transition render settings that will disapear when strata gets enabled

static TAutoConsoleVariable<int32> CVarMaterialRoughDiffuse(
	TEXT("r.Material.RoughDiffuse"),
	0,
	TEXT("Enable rough diffuse material."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);



IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FStrataGlobalUniformParameters, "Strata");



void FStrataSceneData::Reset()
{
	ClassificationTexture = nullptr;
	TopLayerNormalTexture = nullptr;
	SSSTexture = nullptr;

	ClassificationTextureUAV = nullptr;
	TopLayerNormalTextureUAV = nullptr;
	SSSTextureUAV = nullptr;

	MaterialLobesBuffer = nullptr;
	MaterialLobesBufferUAV = nullptr;
	MaterialLobesBufferSRV = nullptr;

	for (uint32 i = 0; i < EStrataTileMaterialType::ECount; ++i)
	{
		ClassificationTileListBuffer[i] = nullptr;
		ClassificationTileListBufferUAV[i] = nullptr;
		ClassificationTileListBufferSRV[i] = nullptr;
		ClassificationTileIndirectBuffer[i] = nullptr;
		ClassificationTileIndirectBufferUAV[i] = nullptr;
		ClassificationTileIndirectBufferSRV[i] = nullptr;
	}

	StrataGlobalUniformParameters = nullptr;
}

const TCHAR* ToString(EStrataTileMaterialType Type)
{
	switch (Type)
	{
	case EStrataTileMaterialType::ESimple:		return TEXT("Simple");
	case EStrataTileMaterialType::EComplex:		return TEXT("Complex");
	}
	return TEXT("Unknown");
}



namespace Strata
{

// Forward declaration
static void AddStrataClearMaterialBufferPass(FRDGBuilder& GraphBuilder, FRDGBufferUAVRef MaterialLobesBufferUAV, uint32 MaxBytesPerPixel, FIntPoint TiledViewBufferResolution);

bool IsStrataEnabled()
{
	return CVarStrata.GetValueOnAnyThread() > 0;
}

bool IsClassificationEnabled()
{
	return CVarStrataClassification.GetValueOnAnyThread() > 0;
}

bool ShouldPassesReadingStrataBeTiled(ERHIFeatureLevel::Type FeatureLevel)
{
	return IsStrataEnabled() && IsClassificationEnabled() && FeatureLevel >= ERHIFeatureLevel::SM5 && CVarStrataClassificationPassesReadingStrataAreTiled.GetValueOnAnyThread() > 0;
}

uint32 GetStrataBufferTileSize()
{
	return 8;
}


void InitialiseStrataFrameSceneData(FSceneRenderer& SceneRenderer, FRDGBuilder& GraphBuilder)
{
	FStrataSceneData& StrataSceneData = SceneRenderer.Scene->StrataSceneData;
	StrataSceneData.Reset();

	auto UpdateMaterialBufferToTiledResolution = [](FIntPoint InBufferSizeXY, FIntPoint& OutMaterialBufferSizeXY)
	{
		// We need to allocate enough for the tiled memory addressing to always work
		OutMaterialBufferSizeXY.X = FMath::DivideAndRoundUp(InBufferSizeXY.X, STRATA_DATA_TILE_SIZE) * STRATA_DATA_TILE_SIZE;
		OutMaterialBufferSizeXY.Y = FMath::DivideAndRoundUp(InBufferSizeXY.Y, STRATA_DATA_TILE_SIZE) * STRATA_DATA_TILE_SIZE;
	};

	FIntPoint MaterialBufferSizeXY;
	UpdateMaterialBufferToTiledResolution(FIntPoint(1, 1), MaterialBufferSizeXY);
	if (IsStrataEnabled())
	{
		FIntPoint SceneTextureExtent = GetSceneTextureExtent();
		
		// We need to allocate enough for the tiled memory addressing of material data to always work
		UpdateMaterialBufferToTiledResolution(SceneTextureExtent, MaterialBufferSizeXY);

		const uint32 MaterialConservativeByteCountPerPixel = CVarStrataBytePerPixel.GetValueOnAnyThread();
		const uint32 RoundToValue = 4u;
		StrataSceneData.MaxBytesPerPixel = FMath::DivideAndRoundUp(MaterialConservativeByteCountPerPixel, RoundToValue) * RoundToValue;

		// Classification texture
		{
			StrataSceneData.ClassificationTexture = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(SceneTextureExtent, PF_R16_UINT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV), TEXT("Strata.ClassificationTexture"));
			StrataSceneData.ClassificationTextureUAV = GraphBuilder.CreateUAV(StrataSceneData.ClassificationTexture);
		}

		// Tile classification buffers
		{
			const int32 TileInPixel = GetStrataBufferTileSize();
			const FIntPoint TileResolution(FMath::DivideAndRoundUp(SceneTextureExtent.X, TileInPixel), FMath::DivideAndRoundUp(SceneTextureExtent.Y, TileInPixel));

			// As of today we allocate one index+indirect buffer for each EStrataTileMaterialType.
			// This is fine for two types, later we might want to have a single list and indirect buffer with offsets.
			for (uint32 i = 0; i < EStrataTileMaterialType::ECount; ++i)
			{
				StrataSceneData.ClassificationTileListBuffer[i] = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), TileResolution.X * TileResolution.Y), i==EStrataTileMaterialType::ESimple ? TEXT("Strata.SimpleStrataTileListBuffer") : TEXT("Strata.ComplexStrataTileListBuffer"));
				StrataSceneData.ClassificationTileListBufferSRV[i] = GraphBuilder.CreateSRV(StrataSceneData.ClassificationTileListBuffer[i], PF_R32_UINT);
				StrataSceneData.ClassificationTileListBufferUAV[i] = GraphBuilder.CreateUAV(StrataSceneData.ClassificationTileListBuffer[i], PF_R32_UINT);

				StrataSceneData.ClassificationTileIndirectBuffer[i] = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDrawIndirectParameters>(), i == EStrataTileMaterialType::ESimple ? TEXT("Strata.SimpleStrataTileIndirectBuffer") : TEXT("Strata.ComplexStrataTileIndirectBuffer"));
				StrataSceneData.ClassificationTileIndirectBufferSRV[i] = GraphBuilder.CreateSRV(StrataSceneData.ClassificationTileIndirectBuffer[i], PF_R32_UINT);
				StrataSceneData.ClassificationTileIndirectBufferUAV[i] = GraphBuilder.CreateUAV(StrataSceneData.ClassificationTileIndirectBuffer[i], PF_R32_UINT);

				AddClearUAVPass(GraphBuilder, StrataSceneData.ClassificationTileIndirectBufferUAV[i], 0);
			}
		}

		// Top layer texture
		{
			StrataSceneData.TopLayerNormalTexture = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(SceneTextureExtent, PF_R32_UINT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV), TEXT("Strata.TopLayerNormalTexture"));
			StrataSceneData.TopLayerNormalTextureUAV = GraphBuilder.CreateUAV(StrataSceneData.TopLayerNormalTexture);
		}

		// SSS texture
		{
			StrataSceneData.SSSTexture = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(SceneTextureExtent, PF_R32G32_UINT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV), TEXT("Strata.SSSTexture"));
			StrataSceneData.SSSTextureUAV = GraphBuilder.CreateUAV(StrataSceneData.SSSTexture);
		}
	}
	else
	{
		StrataSceneData.MaxBytesPerPixel = 4u;
	}

	// create the material lob buffer for all views
	const uint32 MaterialLobesBufferByteSize = FMath::Max(4u, MaterialBufferSizeXY.X * MaterialBufferSizeXY.Y * StrataSceneData.MaxBytesPerPixel);
	StrataSceneData.MaterialLobesBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateByteAddressDesc(MaterialLobesBufferByteSize), TEXT("Strata.MaterialBuffer"));
	StrataSceneData.MaterialLobesBufferSRV = GraphBuilder.CreateSRV(StrataSceneData.MaterialLobesBuffer);
	StrataSceneData.MaterialLobesBufferUAV = GraphBuilder.CreateUAV(StrataSceneData.MaterialLobesBuffer);

	// Rough diffuse model
	StrataSceneData.bRoughDiffuse = CVarStrataRoughDiffuse.GetValueOnRenderThread() > 0 ? 1u : 0u;

	// Set reference to the Strata data from each view
	for (int32 ViewIndex = 0; ViewIndex < SceneRenderer.Views.Num(); ViewIndex++)
	{
		FViewInfo& View = SceneRenderer.Views[ViewIndex];
		View.StrataSceneData = &SceneRenderer.Scene->StrataSceneData;
	}

	if (IsStrataEnabled())
	{
		AddStrataClearMaterialBufferPass(GraphBuilder, StrataSceneData.MaterialLobesBufferUAV, StrataSceneData.MaxBytesPerPixel, MaterialBufferSizeXY);
	}

	// Create the readable uniform buffers for each views once for all (it is view independent and all the views should be tiled into the render target textures & material buffer)
	if (IsStrataEnabled())
	{
		FStrataGlobalUniformParameters* StrataUniformParameters = GraphBuilder.AllocParameters<FStrataGlobalUniformParameters>();
		BindStrataGlobalUniformParameters(GraphBuilder, &StrataSceneData, *StrataUniformParameters);
		StrataSceneData.StrataGlobalUniformParameters = GraphBuilder.CreateUniformBuffer(StrataUniformParameters);
	}
}

void BindStrataBasePassUniformParameters(FRDGBuilder& GraphBuilder, FStrataSceneData* StrataSceneData, FStrataBasePassUniformParameters& OutStrataUniformParameters)
{
	if (IsStrataEnabled() && StrataSceneData)
	{
		OutStrataUniformParameters.bRoughDiffuse = StrataSceneData->bRoughDiffuse ? 1u : 0u;
		OutStrataUniformParameters.MaxBytesPerPixel = StrataSceneData->MaxBytesPerPixel;
		OutStrataUniformParameters.MaterialLobesBufferUAV = StrataSceneData->MaterialLobesBufferUAV;

		OutStrataUniformParameters.ClassificationTextureUAV = StrataSceneData->ClassificationTextureUAV;
		OutStrataUniformParameters.TopLayerNormalTextureUAV = StrataSceneData->TopLayerNormalTextureUAV;
		OutStrataUniformParameters.SSSTextureUAV = StrataSceneData->SSSTextureUAV;
	}
	else
	{
		FRDGTextureRef DummyWritableTexture = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(FIntPoint(1,1), PF_R32_UINT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV), TEXT("Strata.DummyWritableTexture"));
		FRDGTextureUAVRef DummyWritableUAV = GraphBuilder.CreateUAV(DummyWritableTexture);
		const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);
		OutStrataUniformParameters.bRoughDiffuse = 0u;
		OutStrataUniformParameters.MaxBytesPerPixel = 0;
		OutStrataUniformParameters.MaterialLobesBufferUAV = GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalBuffer(GWhiteVertexBufferWithRDG->Buffer), PF_R32_UINT);
		OutStrataUniformParameters.ClassificationTextureUAV = DummyWritableUAV;
		OutStrataUniformParameters.TopLayerNormalTextureUAV = DummyWritableUAV;
		OutStrataUniformParameters.SSSTextureUAV = DummyWritableUAV;
	}
}

void BindStrataGlobalUniformParameters(FRDGBuilder& GraphBuilder, FStrataSceneData* StrataSceneData, FStrataGlobalUniformParameters& OutStrataUniformParameters)
{
	if (IsStrataEnabled() && StrataSceneData)
	{
		OutStrataUniformParameters.bRoughDiffuse = StrataSceneData->bRoughDiffuse ? 1u : 0u;
		OutStrataUniformParameters.MaxBytesPerPixel = StrataSceneData->MaxBytesPerPixel;
		OutStrataUniformParameters.MaterialLobesBuffer = StrataSceneData->MaterialLobesBufferSRV;
		OutStrataUniformParameters.ClassificationTexture = StrataSceneData->ClassificationTexture;
		OutStrataUniformParameters.TopLayerNormalTexture = StrataSceneData->TopLayerNormalTexture;
		OutStrataUniformParameters.SSSTexture = StrataSceneData->SSSTexture;
	}
	else
	{
		const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);
		OutStrataUniformParameters.bRoughDiffuse = 0;
		OutStrataUniformParameters.MaxBytesPerPixel = 0;
		OutStrataUniformParameters.MaterialLobesBuffer = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(GWhiteVertexBufferWithRDG->Buffer), PF_R32_UINT);
		OutStrataUniformParameters.ClassificationTexture = SystemTextures.Black;
		OutStrataUniformParameters.TopLayerNormalTexture = SystemTextures.DefaultNormal8Bit;
		OutStrataUniformParameters.SSSTexture = SystemTextures.Black;
	}
}

TRDGUniformBufferRef<FStrataGlobalUniformParameters> BindStrataGlobalUniformParameters(FStrataSceneData* StrataSceneData)
{
	check(StrataSceneData);
	check(StrataSceneData->StrataGlobalUniformParameters != nullptr || !IsStrataEnabled());
	return StrataSceneData->StrataGlobalUniformParameters;
}

////////////////////////////////////////////////////////////////////////// 
// Debug

#define VISUALIZE_MATERIAL_PASS_COUNT 3

class FVisualizeMaterialPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVisualizeMaterialPS);
	SHADER_USE_PARAMETER_STRUCT(FVisualizeMaterialPS, FGlobalShader);

	class FBSDFPass : SHADER_PERMUTATION_INT("PERMUTATION_BSDF_PASS", VISUALIZE_MATERIAL_PASS_COUNT);
	using FPermutationDomain = TShaderPermutationDomain<FBSDFPass>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FStrataGlobalUniformParameters, Strata)
		SHADER_PARAMETER_TEXTURE(Texture2D, MiniFontTexture)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderDrawDebug::FShaderParameters, ShaderDrawParameters)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool CanRunStrataVizualizeMaterial(EShaderPlatform Platform)
	{
		// On some consoles, this ALU heavy shader (and with optimisation disables for the sake of low compilation time) would spill registers. So only keep it for the editor.
		return IsPCPlatform(Platform);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return GetMaxSupportedFeatureLevel(Parameters.Platform) >= ERHIFeatureLevel::SM5 && Strata::IsStrataEnabled()
			&& CanRunStrataVizualizeMaterial(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		// Stay debug and skip optimizations to reduce compilation time on this long shader.
		OutEnvironment.CompilerFlags.Add(CFLAG_Debug);
	}
};
IMPLEMENT_GLOBAL_SHADER(FVisualizeMaterialPS, "/Engine/Private/Strata/StrataVisualize.usf", "VisualizeMaterialPS", SF_Pixel);

static void AddVisualizeMaterialPasses(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef SceneColorTexture, EShaderPlatform Platform)
{
	FRHIBlendState* PreMultipliedColorTransmittanceBlend = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_SourceAlpha, BO_Add, BF_Zero, BF_One>::GetRHI();
	if (View.Family->EngineShowFlags.VisualizeStrataMaterial)
	{
		FVisualizeMaterialPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVisualizeMaterialPS::FParameters>();
		PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
		PassParameters->Strata = Strata::BindStrataGlobalUniformParameters(View.StrataSceneData);
		PassParameters->MiniFontTexture = GetMiniFontTexture();
		PassParameters->SceneTextures = GetSceneTextureParameters(GraphBuilder);
		PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneColorTexture, ERenderTargetLoadAction::ELoad);

		if (ShaderDrawDebug::IsEnabled())
		{
			ShaderDrawDebug::SetParameters(GraphBuilder, View.ShaderDrawData, PassParameters->ShaderDrawParameters);
		}

		for (uint32 j = 0; j < VISUALIZE_MATERIAL_PASS_COUNT; ++j)
		{
			FVisualizeMaterialPS::FPermutationDomain PermutationVector;
			PermutationVector.Set<typename FVisualizeMaterialPS::FBSDFPass>(j);
			TShaderMapRef<FVisualizeMaterialPS> PixelShader(View.ShaderMap, PermutationVector);

			FPixelShaderUtils::AddFullscreenPass<FVisualizeMaterialPS>(GraphBuilder, View.ShaderMap, RDG_EVENT_NAME("Strata::VisualizeMaterial"),
				PixelShader, PassParameters, View.ViewRect, PreMultipliedColorTransmittanceBlend);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FStrataClearMaterialBufferCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FStrataClearMaterialBufferCS);
	SHADER_USE_PARAMETER_STRUCT(FStrataClearMaterialBufferCS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer, MaterialLobesBufferUAV)
		SHADER_PARAMETER(uint32, MaxBytesPerPixel)
		SHADER_PARAMETER(FIntPoint, TiledViewBufferResolution)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return GetMaxSupportedFeatureLevel(Parameters.Platform) >= ERHIFeatureLevel::SM5 && Strata::IsStrataEnabled();
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_CLEAR_MATERIAL_BUFFER"), 1);
	}
};
IMPLEMENT_GLOBAL_SHADER(FStrataClearMaterialBufferCS, "/Engine/Private/Strata/StrataMaterialClassification.usf", "ClearMaterialBufferMainCS", SF_Compute);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FStrataMaterialTileClassificationPassCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FStrataMaterialTileClassificationPassCS);
	SHADER_USE_PARAMETER_STRUCT(FStrataMaterialTileClassificationPassCS, FGlobalShader);

	class FWaveOps : SHADER_PERMUTATION_BOOL("PERMUTATION_WAVE_OPS");
	using FPermutationDomain = TShaderPermutationDomain<FWaveOps>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER(int32, TileSize)
		SHADER_PARAMETER(int32, bRectPrimitive)
		SHADER_PARAMETER(FIntPoint, ViewResolution)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, ClassificationTexture)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, SimpleTileIndirectDataBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, SimpleTileListDataBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, ComplexTileIndirectDataBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, ComplexTileListDataBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		const bool bUseWaveIntrinsics = FDataDrivenShaderPlatformInfo::GetSupportsWaveOperations(Parameters.Platform);
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FWaveOps>() && !bUseWaveIntrinsics)
		{
			return false;
		}
		return GetMaxSupportedFeatureLevel(Parameters.Platform) >= ERHIFeatureLevel::SM5 && Strata::IsStrataEnabled();
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_TILE_CATEGORIZATION"), 1);

		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FWaveOps>())
		{
			OutEnvironment.CompilerFlags.Add(CFLAG_WaveOperations);
		}
	}
};
IMPLEMENT_GLOBAL_SHADER(FStrataMaterialTileClassificationPassCS, "/Engine/Private/Strata/StrataMaterialClassification.usf", "TileMainCS", SF_Compute);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FStrataMaterialStencilTaggingPassPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FStrataMaterialStencilTaggingPassPS);
	SHADER_USE_PARAMETER_STRUCT(FStrataMaterialStencilTaggingPassPS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(Strata::FStrataTilePassVS::FParameters, VS)
		SHADER_PARAMETER(FVector4f, DebugTileColor)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return GetMaxSupportedFeatureLevel(Parameters.Platform) >= ERHIFeatureLevel::SM5 && Strata::IsStrataEnabled();
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_STENCIL_CATEGORIZATION"), 1); 
	}
};

IMPLEMENT_GLOBAL_SHADER(FStrataTilePassVS, "/Engine/Private/Strata/StrataMaterialClassification.usf", "StrataTilePassVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FStrataMaterialStencilTaggingPassPS, "/Engine/Private/Strata/StrataMaterialClassification.usf", "StencilMainPS", SF_Pixel);

void FillUpTiledPassData(
	EStrataTileMaterialType Type, 
	const FViewInfo& View, 
	FStrataTilePassVS::FParameters& ParametersVS,
	EPrimitiveType& PrimitiveType)
{
	ParametersVS.OutputViewSizeAndInvSize = View.CachedViewUniformShaderParameters->ViewSizeAndInvSize;
	ParametersVS.OutputBufferSizeAndInvSize = View.CachedViewUniformShaderParameters->BufferSizeAndInvSize;
	ParametersVS.ViewScreenToTranslatedWorld = View.CachedViewUniformShaderParameters->ScreenToTranslatedWorld;

	ParametersVS.TileListBuffer = View.StrataSceneData->ClassificationTileListBufferSRV[Type];
	ParametersVS.TileIndirectBuffer = View.StrataSceneData->ClassificationTileIndirectBuffer[Type];

	PrimitiveType = GRHISupportsRectTopology ? PT_RectList : PT_TriangleList;
}

static void AddStrataInternalClassificationTilePass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FRDGTextureRef* DepthTexture,
	const FRDGTextureRef* ColorTexture,
	EStrataTileMaterialType TileMaterialType,
	const bool bDebug = false)
{
	// We cannot early exit due to the fact that the local light are still rendered as mesh volumes (so cannot be tiled as such)
	//if (ShouldPassesReadingStrataBeTiled())
	//{
	//	return;
	//}

	EPrimitiveType StrataTilePrimitiveType = PT_TriangleList;
	const FIntPoint OutputResolution = View.ViewRect.Size();
	FVector4f OutputResolutionAndInv = FVector4f(OutputResolution.X, OutputResolution.Y, 1.0f / float(OutputResolution.X), 1.0f / float(OutputResolution.Y));

	FStrataMaterialStencilTaggingPassPS::FParameters* ParametersPS = GraphBuilder.AllocParameters<FStrataMaterialStencilTaggingPassPS::FParameters>();
	FillUpTiledPassData(TileMaterialType, View, ParametersPS->VS, StrataTilePrimitiveType);

	FStrataTilePassVS::FPermutationDomain VSPermutationVector;
	VSPermutationVector.Set< FStrataTilePassVS::FEnableDebug >(bDebug);
	VSPermutationVector.Set< FStrataTilePassVS::FEnableTexCoordScreenVector >(false);
	TShaderMapRef<FStrataTilePassVS> VertexShader(View.ShaderMap, VSPermutationVector);
	TShaderMapRef<FStrataMaterialStencilTaggingPassPS> PixelShader(View.ShaderMap);

	// For debug purpose
	if (bDebug)
	{
		check(ColorTexture);
		ParametersPS->RenderTargets[0] = FRenderTargetBinding(*ColorTexture, ERenderTargetLoadAction::ELoad);
		switch (TileMaterialType)
		{
		case EStrataTileMaterialType::ESimple:
			ParametersPS->DebugTileColor = FVector4f(0.0f, 1.0f, 0.0f, 1.0);
			break;
		case EStrataTileMaterialType::EComplex:
			ParametersPS->DebugTileColor = FVector4f(1.0f, 0.0f, 0.0f, 1.0);
			break;
		default:
			check(false);
		}
	}
	else
	{
		check(DepthTexture);
		ParametersPS->RenderTargets.DepthStencil = FDepthStencilBinding(
			*DepthTexture,
			ERenderTargetLoadAction::ELoad,
			ERenderTargetLoadAction::ELoad,
			FExclusiveDepthStencil::DepthNop_StencilWrite);
		ParametersPS->DebugTileColor = FVector4f(ForceInitToZero);
	}
	
	GraphBuilder.AddPass(
		bDebug ? 
		RDG_EVENT_NAME("Strata::DebugClassificationPass") :
		RDG_EVENT_NAME("Strata::StencilClassificationPass"),
		ParametersPS,
		ERDGPassFlags::Raster,
		[ParametersPS, VertexShader, PixelShader, OutputResolution, StrataTilePrimitiveType, bDebug](FRHICommandList& RHICmdList)
		{
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			if (bDebug)
			{
				// Use premultiplied alpha blending, pixel shader and depth/stencil is off
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
				GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One>::GetRHI();
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			}
			else
			{
				// No blending and no pixel shader required. Stencil will be writen to.
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = nullptr;
				GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<
					false, CF_Always, 
					true,  CF_Always, SO_Keep, SO_Keep, SO_Replace,
					false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
					0xFF, StencilBit>::GetRHI();
			}
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.PrimitiveType = StrataTilePrimitiveType;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, StencilBit);
			SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), ParametersPS->VS);
			if (bDebug)
			{
				SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *ParametersPS);
			}

			RHICmdList.SetViewport(0, 0, 0.0f, OutputResolution.X, OutputResolution.Y, 1.0f);
			RHICmdList.SetStreamSource(0, nullptr, 0);
			RHICmdList.DrawPrimitiveIndirect(ParametersPS->VS.TileIndirectBuffer->GetIndirectRHICallBuffer(), 0);
		});
}

void AddStrataStencilPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FMinimalSceneTextures& SceneTextures)
{
	AddStrataInternalClassificationTilePass(GraphBuilder, View, &SceneTextures.Depth.Target, nullptr, EStrataTileMaterialType::ESimple);
}

void AddStrataStencilPass(
	FRDGBuilder& GraphBuilder,
	const TArray<FViewInfo>& Views,
	const FMinimalSceneTextures& SceneTextures)
{
	for (int32 i = 0; i < Views.Num(); ++i)
	{
		const FViewInfo& View = Views[i];
		AddStrataInternalClassificationTilePass(GraphBuilder, View, &SceneTextures.Depth.Target, nullptr, EStrataTileMaterialType::ESimple);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void AddStrataMaterialClassificationPass(FRDGBuilder& GraphBuilder, const FMinimalSceneTextures& SceneTextures, const TArray<FViewInfo>& Views)
{
	RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, IsStrataEnabled() && Views.Num() > 0, "StrataMaterialClassification");
	if (!IsStrataEnabled())
	{
		return;
	}

	for (int32 i = 0; i < Views.Num(); ++i)
	{
		const FViewInfo& View = Views[i];
		
		// Tile reduction
		if (IsClassificationEnabled())
		{
			bool bWaveOps = FDataDrivenShaderPlatformInfo::GetSupportsWaveOperations(View.GetShaderPlatform());
		#if PLATFORM_WINDOWS
			// Tile reduction requires 64-wide wave
			bWaveOps = bWaveOps && !IsRHIDeviceNVIDIA();
		#endif

			FStrataMaterialTileClassificationPassCS::FPermutationDomain PermutationVector;
			PermutationVector.Set< FStrataMaterialTileClassificationPassCS::FWaveOps >(bWaveOps);
			TShaderMapRef<FStrataMaterialTileClassificationPassCS> ComputeShader(View.ShaderMap, PermutationVector);
			FStrataMaterialTileClassificationPassCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FStrataMaterialTileClassificationPassCS::FParameters>();
			PassParameters->TileSize = GetStrataBufferTileSize();	// STRATA_TODO not sure we want to tie the buffer tile optimisation for cache and the Categotisation tile size?
			PassParameters->bRectPrimitive = GRHISupportsRectTopology ? 1 : 0;
			PassParameters->ViewResolution = View.ViewRect.Size();
			PassParameters->ClassificationTexture = View.StrataSceneData->ClassificationTexture;
			PassParameters->SimpleTileListDataBuffer = View.StrataSceneData->ClassificationTileListBufferUAV[EStrataTileMaterialType::ESimple];
			PassParameters->SimpleTileIndirectDataBuffer = View.StrataSceneData->ClassificationTileIndirectBufferUAV[EStrataTileMaterialType::ESimple];
			PassParameters->ComplexTileListDataBuffer = View.StrataSceneData->ClassificationTileListBufferUAV[EStrataTileMaterialType::EComplex];
			PassParameters->ComplexTileIndirectDataBuffer = View.StrataSceneData->ClassificationTileIndirectBufferUAV[EStrataTileMaterialType::EComplex];

			const uint32 GroupSize = 8;
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Strata::MaterialTileClassification(%s)", bWaveOps ? TEXT("Wave") : TEXT("SharedMemory")),
				ComputeShader,
				PassParameters,
				FComputeShaderUtils::GetGroupCount(View.StrataSceneData->ClassificationTexture->Desc.Extent, GroupSize));
		}
	}
}

static void AddStrataClearMaterialBufferPass(FRDGBuilder& GraphBuilder, FRDGBufferUAVRef MaterialLobesBufferUAV, uint32 MaxBytesPerPixel, FIntPoint TiledViewBufferResolution)
{
	TShaderMapRef<FStrataClearMaterialBufferCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	FStrataClearMaterialBufferCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FStrataClearMaterialBufferCS::FParameters>();
	PassParameters->MaterialLobesBufferUAV = MaterialLobesBufferUAV;
	PassParameters->MaxBytesPerPixel = MaxBytesPerPixel;
	PassParameters->TiledViewBufferResolution = TiledViewBufferResolution;

	const uint32 GroupSize = 8;
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("Strata::ClearMaterialBuffer"),
		ComputeShader,
		PassParameters,
		FComputeShaderUtils::GetGroupCount(TiledViewBufferResolution, GroupSize));
}

bool ShouldRenderStrataDebugPasses(const FViewInfo& View)
{
	return IsStrataEnabled() &&
		(
		   (FVisualizeMaterialPS::CanRunStrataVizualizeMaterial(View.GetShaderPlatform()) && View.Family && View.Family->EngineShowFlags.VisualizeStrataMaterial)
		|| (IsClassificationEnabled() && CVarStrataClassificationDebug.GetValueOnAnyThread() > 0)
		);
}

FScreenPassTexture AddStrataDebugPasses(FRDGBuilder& GraphBuilder, const FViewInfo& View, FScreenPassTexture& ScreenPassSceneColor)
{
	check(IsStrataEnabled());
	EShaderPlatform Platform = View.GetShaderPlatform();

	if (FVisualizeMaterialPS::CanRunStrataVizualizeMaterial(Platform))
	{
		RDG_EVENT_SCOPE(GraphBuilder, "Strata::VisualizeMaterial");
		AddVisualizeMaterialPasses(GraphBuilder, View, ScreenPassSceneColor.Texture, Platform);
	}

	const int32 StrataClassificationDebug = CVarStrataClassificationDebug.GetValueOnAnyThread();
	if (IsClassificationEnabled() && StrataClassificationDebug > 0)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "Strata::VisualizeClassification");
		const bool bDebugPass = true;
		AddStrataInternalClassificationTilePass(
			GraphBuilder, View, nullptr, &ScreenPassSceneColor.Texture, EStrataTileMaterialType::ESimple, bDebugPass);
		AddStrataInternalClassificationTilePass(
			GraphBuilder, View, nullptr, &ScreenPassSceneColor.Texture, EStrataTileMaterialType::EComplex, bDebugPass);
	}

	return MoveTemp(ScreenPassSceneColor);
}

} // namespace Strata


