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

static TAutoConsoleVariable<int32> CVarStrataLUTResolution(
	TEXT("r.Strata.LUT.Resolution"),
	64,
	TEXT("Resolution of the GGX energy LUT."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarStrataLUTSampleCount(
	TEXT("r.Strata.LUT.SampleCount"),
	128,
	TEXT("Number of sample used for computing the energy LUT."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarStrataLUTContinousUpdate(
	TEXT("r.Strata.LUT.ContinousUpdate"),
	0,
	TEXT("Update Strata energy LUT every frame (for debug purpose)."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarStrataFurnaceTest(
	TEXT("r.Strata.FurnaceTest"),
	0,
	TEXT("Enable Strata furnace test (for debug purpose) 1:roughness/metallic, 2:roughness/aniso, 3:roughness/haze, 4:a selection of conductors."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarStrataFurnaceTestIntegratorType(
	TEXT("r.Strata.FurnaceTest.IntegratorType"),
	0,
	TEXT("Change Strata furnace test integrator (for debug purpose) 0: evaluate integrator 1: importance sampling integrator 2: env. integrator."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarStrataFurnaceTestSampleCount(
	TEXT("r.Strata.FurnaceTest.SampleCount"),
	1024,
	TEXT("Number of sample used for furnace test."),
	ECVF_RenderThreadSafe);

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FStrataGlobalUniformParameters, "Strata");



void FStrataSceneData::Reset()
{
	ClassificationTexture = nullptr;
	TopLayerNormalTexture = nullptr;
	SSSTexture = nullptr;

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



namespace Strata
{

// Forward declaration
static void AddStrataClearMaterialBufferPass(FRDGBuilder& GraphBuilder, FRDGBufferUAVRef MaterialLobesBufferUAV, uint32 MaxBytesPerPixel, FIntPoint TiledViewBufferResolution);
static void AddStrataLUTPass(FRDGBuilder& GraphBuilder, FSceneRenderer& SceneRenderer, TRefCountPtr<IPooledRenderTarget>& GGXEnergyLUT2DTexture, TRefCountPtr<IPooledRenderTarget>& GGXEnergyLUT3DTexture);

static uint32 GetStrataGGXEnergyLUTResolution()
{
	return FMath::RoundUpToPowerOfTwo(FMath::Clamp(CVarStrataLUTResolution.GetValueOnAnyThread(), 16, 256));
}

static FVector2D GetStrataGGXEnergyLUTScaleBias()
{
	const float Resolution = GetStrataGGXEnergyLUTResolution();
	const float Scale = (Resolution - 1) / Resolution;
	const float Bias  = 0.5f / (Resolution - 1);
	return FVector2D(Scale, Bias);
}

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
	bool bUpdateLUT = false;
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
			StrataSceneData.ClassificationTexture = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(SceneTextureExtent, PF_R32_UINT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_RenderTargetable), TEXT("StrataClassificationTexture"));
		}

		// Tile classification buffers
		{
			const int32 TileInPixel = GetStrataBufferTileSize();
			const FIntPoint TileResolution(FMath::DivideAndRoundUp(SceneTextureExtent.X, TileInPixel), FMath::DivideAndRoundUp(SceneTextureExtent.Y, TileInPixel));

			// As of today we allocate one index+indirect buffer for each EStrataTileMaterialType.
			// This is fine for two types, later we might want to have a single list and indirect buffer with offsets.
			for (uint32 i = 0; i < EStrataTileMaterialType::ECount; ++i)
			{
				StrataSceneData.ClassificationTileListBuffer[i] = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), TileResolution.X * TileResolution.Y), i==EStrataTileMaterialType::ESimple ? TEXT("SimpleStrataTileListBuffer") : TEXT("ComplexStrataTileListBuffer"));
				StrataSceneData.ClassificationTileListBufferSRV[i] = GraphBuilder.CreateSRV(StrataSceneData.ClassificationTileListBuffer[i], PF_R32_UINT);
				StrataSceneData.ClassificationTileListBufferUAV[i] = GraphBuilder.CreateUAV(StrataSceneData.ClassificationTileListBuffer[i], PF_R32_UINT);

				StrataSceneData.ClassificationTileIndirectBuffer[i] = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDrawIndirectParameters>(), i == EStrataTileMaterialType::ESimple ? TEXT("SimpleStrataTileIndirectBuffer") : TEXT("ComplexStrataTileIndirectBuffer"));
				StrataSceneData.ClassificationTileIndirectBufferSRV[i] = GraphBuilder.CreateSRV(StrataSceneData.ClassificationTileIndirectBuffer[i], PF_R32_UINT);
				StrataSceneData.ClassificationTileIndirectBufferUAV[i] = GraphBuilder.CreateUAV(StrataSceneData.ClassificationTileIndirectBuffer[i], PF_R32_UINT);

				AddClearUAVPass(GraphBuilder, StrataSceneData.ClassificationTileIndirectBufferUAV[i], 0);
			}
		}

		// Top layer texture
		{
			StrataSceneData.TopLayerNormalTexture = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(SceneTextureExtent, PF_R32_UINT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_RenderTargetable), TEXT("StrataTopLayerNormalTexture"));
		}

		// SSS texture
		{
			StrataSceneData.SSSTexture = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(SceneTextureExtent, PF_R32G32_UINT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_RenderTargetable), TEXT("StrataSSSTexture"));
		}

		// Energy LUT
		const uint32 LUTResolution = GetStrataGGXEnergyLUTResolution();
		bUpdateLUT = StrataSceneData.GGXEnergyLUT2DTexture == nullptr || StrataSceneData.GGXEnergyLUT2DTexture->GetDesc().Extent.X != LUTResolution || CVarStrataLUTContinousUpdate.GetValueOnAnyThread() > 0;
		if (bUpdateLUT)
		{
			FRDGTextureDesc Desc3D = FRDGTextureDesc::Create3D(FIntVector(LUTResolution, LUTResolution, LUTResolution), EPixelFormat::PF_G16R16F, FClearValueBinding::Black, ETextureCreateFlags::TexCreate_ShaderResource | ETextureCreateFlags::TexCreate_RenderTargetable | ETextureCreateFlags::TexCreate_UAV);
			FRDGTextureDesc Desc2D = FRDGTextureDesc::Create2D(FIntPoint(LUTResolution, LUTResolution), EPixelFormat::PF_FloatRGBA, FClearValueBinding::Black, ETextureCreateFlags::TexCreate_ShaderResource | ETextureCreateFlags::TexCreate_RenderTargetable | ETextureCreateFlags::TexCreate_UAV);
			FRDGTextureRef OutLUT3D = GraphBuilder.CreateTexture(Desc3D, TEXT("StrataEnergyLUT3D"));
			FRDGTextureRef OutLUT2D = GraphBuilder.CreateTexture(Desc2D, TEXT("StrataEnergyLUT2D"));

			StrataSceneData.GGXEnergyLUT3DTexture = GraphBuilder.ConvertToExternalTexture(OutLUT3D);
			StrataSceneData.GGXEnergyLUT2DTexture = GraphBuilder.ConvertToExternalTexture(OutLUT2D);
		}
	}
	else
	{
		StrataSceneData.MaxBytesPerPixel = 4u;
	}

	// create the material lob buffer for all views
	const uint32 MaterialLobesBufferByteSize = FMath::Max(4u, MaterialBufferSizeXY.X * MaterialBufferSizeXY.Y * StrataSceneData.MaxBytesPerPixel);
	StrataSceneData.MaterialLobesBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateByteAddressDesc(MaterialLobesBufferByteSize), TEXT("StrataMaterialBuffer"));
	StrataSceneData.MaterialLobesBufferSRV = GraphBuilder.CreateSRV(StrataSceneData.MaterialLobesBuffer);
	StrataSceneData.MaterialLobesBufferUAV = GraphBuilder.CreateUAV(StrataSceneData.MaterialLobesBuffer);

	// Set reference to the Strata data from each view
	for (int32 ViewIndex = 0; ViewIndex < SceneRenderer.Views.Num(); ViewIndex++)
	{
		FViewInfo& View = SceneRenderer.Views[ViewIndex];
		View.StrataSceneData = &SceneRenderer.Scene->StrataSceneData;
	}

	if (IsStrataEnabled())
	{
		AddStrataClearMaterialBufferPass(GraphBuilder, StrataSceneData.MaterialLobesBufferUAV, StrataSceneData.MaxBytesPerPixel, MaterialBufferSizeXY);
		if (bUpdateLUT)
		{
			AddStrataLUTPass(GraphBuilder, SceneRenderer, StrataSceneData.GGXEnergyLUT2DTexture, StrataSceneData.GGXEnergyLUT3DTexture);
		}
	}

	// Create the readable uniform buffers for each views once for all (it is view independent and all the views should be tiled into the render target textures & material buffer)
	if (IsStrataEnabled())
	{
		FStrataGlobalUniformParameters* StrataUniformParameters = GraphBuilder.AllocParameters<FStrataGlobalUniformParameters>();
		StrataUniformParameters->MaxBytesPerPixel = StrataSceneData.MaxBytesPerPixel;
		StrataUniformParameters->MaterialLobesBuffer = StrataSceneData.MaterialLobesBufferSRV;
		StrataUniformParameters->ClassificationTexture = StrataSceneData.ClassificationTexture;
		StrataUniformParameters->TopLayerNormalTexture = StrataSceneData.TopLayerNormalTexture;
		StrataUniformParameters->SSSTexture = StrataSceneData.SSSTexture;
		StrataUniformParameters->GGXEnergyLUT3DTexture = StrataSceneData.GGXEnergyLUT3DTexture->GetRenderTargetItem().ShaderResourceTexture;
		StrataUniformParameters->GGXEnergyLUT2DTexture = StrataSceneData.GGXEnergyLUT2DTexture->GetRenderTargetItem().ShaderResourceTexture;
		StrataUniformParameters->GGXEnergyLUTSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		StrataUniformParameters->GGXEnergyLUTScaleBias = GetStrataGGXEnergyLUTScaleBias();
		StrataSceneData.StrataGlobalUniformParameters = GraphBuilder.CreateUniformBuffer(StrataUniformParameters);
	}
}

void BindStrataBasePassUniformParameters(FRDGBuilder& GraphBuilder, FStrataSceneData* StrataSceneData, FStrataBasePassUniformParameters& OutStrataUniformParameters)
{
	OutStrataUniformParameters.GGXEnergyLUTScaleBias = GetStrataGGXEnergyLUTScaleBias();
	OutStrataUniformParameters.GGXEnergyLUTSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	if (IsStrataEnabled() && StrataSceneData)
	{
		OutStrataUniformParameters.MaxBytesPerPixel = StrataSceneData->MaxBytesPerPixel;
		OutStrataUniformParameters.MaterialLobesBufferUAV = StrataSceneData->MaterialLobesBufferUAV;
		OutStrataUniformParameters.GGXEnergyLUT3DTexture = StrataSceneData->GGXEnergyLUT3DTexture->GetRenderTargetItem().ShaderResourceTexture;
		OutStrataUniformParameters.GGXEnergyLUT2DTexture = StrataSceneData->GGXEnergyLUT2DTexture->GetRenderTargetItem().ShaderResourceTexture;
	}
	else
	{
		OutStrataUniformParameters.MaxBytesPerPixel = 0;
		OutStrataUniformParameters.MaterialLobesBufferUAV = GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalBuffer(GWhiteVertexBufferWithRDG->Buffer), PF_R32_UINT);
		OutStrataUniformParameters.GGXEnergyLUT3DTexture = GSystemTextures.VolumetricBlackDummy->GetRenderTargetItem().ShaderResourceTexture;
		OutStrataUniformParameters.GGXEnergyLUT2DTexture = GSystemTextures.BlackDummy->GetRenderTargetItem().ShaderResourceTexture;
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
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderDrawDebug::FShaderDrawDebugParameters, ShaderDrawParameters)
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

		if (ShaderDrawDebug::IsShaderDrawDebugEnabled())
		{
			ShaderDrawDebug::SetParameters(GraphBuilder, View.ShaderDrawData, PassParameters->ShaderDrawParameters);
		}

		for (uint32 j = 0; j < VISUALIZE_MATERIAL_PASS_COUNT; ++j)
		{
			FVisualizeMaterialPS::FPermutationDomain PermutationVector;
			PermutationVector.Set<typename FVisualizeMaterialPS::FBSDFPass>(j);
			TShaderMapRef<FVisualizeMaterialPS> PixelShader(View.ShaderMap, PermutationVector);

			FPixelShaderUtils::AddFullscreenPass<FVisualizeMaterialPS>(GraphBuilder, View.ShaderMap, RDG_EVENT_NAME("StrataVisualizeMaterial"),
				PixelShader, PassParameters, View.ViewRect, PreMultipliedColorTransmittanceBlend);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// Material classification pass
// * Classification texture (shading models, BSDF bits, ...)
// For future:
// * SSS: hasSSS, Normal, ProfilID, BaseColor, Opacity,  MFPAlbedo/MFPRadius, Shadingmodel | 64bit?
// * SSR: depth, roughness, normal, (clear coat amount/roughness), tangent, aniso

// SSS/SSR/Auxilary data (AO/ShadowMask/...)
class FStrataMaterialClassificationPassPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FStrataMaterialClassificationPassPS);
	SHADER_USE_PARAMETER_STRUCT(FStrataMaterialClassificationPassPS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER(uint32, MaxBytesPerPixel)
		SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, MaterialLobesBuffer)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderDrawDebug::FShaderDrawDebugParameters, ShaderDrawParameters)
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
		OutEnvironment.SetDefine(TEXT("SHADER_CATEGORIZATION"), 1);
		OutEnvironment.SetRenderTargetOutputFormat(0, PF_R32_UINT);
		OutEnvironment.SetRenderTargetOutputFormat(1, PF_R32_UINT);
		OutEnvironment.SetRenderTargetOutputFormat(2, PF_R32G32_UINT);
	}
};
IMPLEMENT_GLOBAL_SHADER(FStrataMaterialClassificationPassPS, "/Engine/Private/Strata/StrataMaterialClassification.usf", "MainPS", SF_Pixel);

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

	using FPermutationDomain = TShaderPermutationDomain<>;

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
		OutEnvironment.SetDefine(TEXT("SHADER_TILE_CATEGORIZATION"), 1);
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
		SHADER_PARAMETER(FVector4, DebugTileColor)
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
	FVector4 OutputResolutionAndInv = FVector4(OutputResolution.X, OutputResolution.Y, 1.0f / float(OutputResolution.X), 1.0f / float(OutputResolution.Y));

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
			ParametersPS->DebugTileColor = FVector4(0.0f, 1.0f, 0.0f, 1.0);
			break;
		case EStrataTileMaterialType::EComplex:
			ParametersPS->DebugTileColor = FVector4(1.0f, 0.0f, 0.0f, 1.0);
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
		ParametersPS->DebugTileColor = FVector4(ForceInitToZero);
	}
	
	GraphBuilder.AddPass(
		bDebug ? 
		RDG_EVENT_NAME("StrataDebugClassificationPass") :
		RDG_EVENT_NAME("StrataStencilClassificationPass"),
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
				SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *ParametersPS);
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
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
			SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), ParametersPS->VS);

			RHICmdList.SetStencilRef(StencilBit);
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
		
		// Classification
		{
			FStrataMaterialClassificationPassPS::FPermutationDomain PermutationVector;
			TShaderMapRef<FStrataMaterialClassificationPassPS> PixelShader(View.ShaderMap, PermutationVector);
			FStrataMaterialClassificationPassPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FStrataMaterialClassificationPassPS::FParameters>();
			PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
			PassParameters->MaxBytesPerPixel = View.StrataSceneData->MaxBytesPerPixel;
			PassParameters->MaterialLobesBuffer = View.StrataSceneData->MaterialLobesBufferSRV;
			PassParameters->SceneTextures = GetSceneTextureParameters(GraphBuilder);		
			PassParameters->RenderTargets[0] = FRenderTargetBinding(View.StrataSceneData->ClassificationTexture, ERenderTargetLoadAction::EClear);
			PassParameters->RenderTargets[1] = FRenderTargetBinding(View.StrataSceneData->TopLayerNormalTexture, ERenderTargetLoadAction::EClear);
			PassParameters->RenderTargets[2] = FRenderTargetBinding(View.StrataSceneData->SSSTexture, ERenderTargetLoadAction::EClear);

			if (ShaderDrawDebug::IsShaderDrawDebugEnabled())
			{
				ShaderDrawDebug::SetParameters(GraphBuilder, View.ShaderDrawData, PassParameters->ShaderDrawParameters);
			}

			FPixelShaderUtils::AddFullscreenPass<FStrataMaterialClassificationPassPS>(
				GraphBuilder, 
				View.ShaderMap, 
				RDG_EVENT_NAME("StrataMaterialClassification"),
				PixelShader, 
				PassParameters, 
				View.ViewRect);
		}

		// Downsampling
		if (IsClassificationEnabled())
		{
			TShaderMapRef<FStrataMaterialTileClassificationPassCS> ComputeShader(View.ShaderMap);
			FStrataMaterialTileClassificationPassCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FStrataMaterialTileClassificationPassCS::FParameters>();
			PassParameters->TileSize = GetStrataBufferTileSize();	// STRATA_TODO not sure we want to tie the buffer tile optimisation for cache and the Categotisation tile size?
			PassParameters->bRectPrimitive = GRHISupportsRectTopology ? 1 : 0;
			PassParameters->ViewResolution = View.ViewRect.Size();
			PassParameters->ClassificationTexture = View.StrataSceneData->ClassificationTexture;
			PassParameters->SimpleTileListDataBuffer = View.StrataSceneData->ClassificationTileListBufferUAV[EStrataTileMaterialType::ESimple];
			PassParameters->SimpleTileIndirectDataBuffer = View.StrataSceneData->ClassificationTileIndirectBufferUAV[EStrataTileMaterialType::ESimple];
			PassParameters->ComplexTileListDataBuffer = View.StrataSceneData->ClassificationTileListBufferUAV[EStrataTileMaterialType::EComplex];
			PassParameters->ComplexTileIndirectDataBuffer = View.StrataSceneData->ClassificationTileIndirectBufferUAV[EStrataTileMaterialType::EComplex];

			// Add 64 threads permutation
			const uint32 GroupSize = 8;
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("StrataMaterialTileClassification"),
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
		RDG_EVENT_NAME("StrataClearMaterialBuffer"),
		ComputeShader,
		PassParameters,
		FComputeShaderUtils::GetGroupCount(TiledViewBufferResolution, GroupSize));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FStrataLUTPassPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FStrataLUTPassPS);
	SHADER_USE_PARAMETER_STRUCT(FStrataLUTPassPS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, SliceXYCount)
		SHADER_PARAMETER(uint32, EnergyLUTResolution)
		SHADER_PARAMETER(uint32, NumSamples)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D, OutLUT3D)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutLUT2D)
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
		OutEnvironment.SetDefine(TEXT("SHADER_LUT"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FStrataLUTPassPS, "/Engine/Private/Strata/StrataLUT.usf", "MainPS", SF_Pixel);


static void AddStrataLUTPass(FRDGBuilder& GraphBuilder, FSceneRenderer& SceneRenderer, TRefCountPtr<IPooledRenderTarget>& GGXEnergyLUT2DTexture, TRefCountPtr<IPooledRenderTarget>& GGXEnergyLUT3DTexture)
{
	const uint32 LUTResolution = GetStrataGGXEnergyLUTResolution();
	const uint32 SliceResolution = FMath::CeilToInt(FMath::Sqrt(float(LUTResolution)));
	
	FIntPoint OutputResolutionRT;
	OutputResolutionRT.X = LUTResolution * SliceResolution;
	OutputResolutionRT.Y = LUTResolution * SliceResolution;
	FIntRect RenderTargetRect = FIntRect(FIntPoint(ForceInitToZero), OutputResolutionRT);

	FRDGTextureRef OutLUT2D = GraphBuilder.RegisterExternalTexture(GGXEnergyLUT2DTexture);
	FRDGTextureRef OutLUT3D = GraphBuilder.RegisterExternalTexture(GGXEnergyLUT3DTexture);

	// For debug purpose	
	FRDGTextureDesc UnfoldLUTDesc = FRDGTextureDesc::Create2D(OutputResolutionRT, EPixelFormat::PF_G16R16F, FClearValueBinding::Black, ETextureCreateFlags::TexCreate_RenderTargetable);
	FRDGTextureRef UnfoldLUTTexture = GraphBuilder.CreateTexture(UnfoldLUTDesc, TEXT("StrataEnergyUnfoldLUT"));

	FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(SceneRenderer.FeatureLevel);
	TShaderMapRef<FStrataLUTPassPS> PixelShader(GlobalShaderMap);
	FStrataLUTPassPS::FParameters* Parameters = GraphBuilder.AllocParameters<FStrataLUTPassPS::FParameters>();
	Parameters->NumSamples = FMath::Clamp(CVarStrataLUTSampleCount.GetValueOnAnyThread(), 16, 2048);
	Parameters->EnergyLUTResolution = LUTResolution;
	Parameters->SliceXYCount = FIntPoint(SliceResolution, SliceResolution);
	Parameters->OutLUT2D = GraphBuilder.CreateUAV(OutLUT2D);
	Parameters->OutLUT3D = GraphBuilder.CreateUAV(OutLUT3D);
	Parameters->RenderTargets[0] = FRenderTargetBinding(UnfoldLUTTexture, ERenderTargetLoadAction::EClear);

	FPixelShaderUtils::AddFullscreenPass<FStrataLUTPassPS>(
		GraphBuilder,
		GlobalShaderMap,
		RDG_EVENT_NAME("StrataLUT"),
		PixelShader,
		Parameters,
		RenderTargetRect);

	// Finalize because these textures lifetime is multiple frame, and also make sure transition is correctly done.
	FRDGResourceAccessFinalizer ResourceAccessFinalizer;
	GGXEnergyLUT2DTexture = ConvertToFinalizedExternalTexture(GraphBuilder, ResourceAccessFinalizer, OutLUT2D, ERHIAccess::SRVMask);
	GGXEnergyLUT3DTexture = ConvertToFinalizedExternalTexture(GraphBuilder, ResourceAccessFinalizer, OutLUT3D, ERHIAccess::SRVMask);
	ResourceAccessFinalizer.Finalize(GraphBuilder);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FStrataFurnaceTestPassPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FStrataFurnaceTestPassPS);
	SHADER_USE_PARAMETER_STRUCT(FStrataFurnaceTestPassPS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FStrataGlobalUniformParameters, Strata)
		SHADER_PARAMETER(uint32, NumSamples)
		SHADER_PARAMETER(uint32, SceneType)
		SHADER_PARAMETER(uint32, IntegratorType)
		SHADER_PARAMETER_TEXTURE(Texture2D, PreIntegratedGF)
		SHADER_PARAMETER_SAMPLER(SamplerState, PreIntegratedGFSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, OutLUT3D)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, OutLUT2D)
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
		OutEnvironment.SetDefine(TEXT("SHADER_FURNACE_ANALYTIC"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FStrataFurnaceTestPassPS, "/Engine/Private/Strata/StrataFurnaceTest.usf", "MainPS", SF_Pixel);

static void AddStrataFurnacePass(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef OutTexture)
{
	TShaderMapRef<FStrataFurnaceTestPassPS> PixelShader(View.ShaderMap);
	FStrataFurnaceTestPassPS::FParameters* Parameters = GraphBuilder.AllocParameters<FStrataFurnaceTestPassPS::FParameters>();
	Parameters->ViewUniformBuffer = View.ViewUniformBuffer;
	Parameters->Strata = Strata::BindStrataGlobalUniformParameters(View.StrataSceneData);
	Parameters->SceneType = FMath::Clamp(CVarStrataFurnaceTest.GetValueOnAnyThread(), 1, 4);
	Parameters->IntegratorType = FMath::Clamp(CVarStrataFurnaceTestIntegratorType.GetValueOnAnyThread(), 0, 2);
	Parameters->PreIntegratedGF = GSystemTextures.PreintegratedGF->GetRenderTargetItem().ShaderResourceTexture;
	Parameters->PreIntegratedGFSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters->NumSamples = FMath::Clamp(CVarStrataFurnaceTestSampleCount.GetValueOnAnyThread(), 16, 2048);
	Parameters->RenderTargets[0] = FRenderTargetBinding(OutTexture, ERenderTargetLoadAction::ELoad);

	FPixelShaderUtils::AddFullscreenPass<FStrataFurnaceTestPassPS>(
		GraphBuilder,
		View.ShaderMap,
		RDG_EVENT_NAME("StrataFurnaceTest"),
		PixelShader,
		Parameters,
		View.ViewRect);
}

void AddStrataDebugPasses(FRDGBuilder& GraphBuilder, const TArray<FViewInfo>& Views, FRDGTextureRef SceneColorTexture, EShaderPlatform Platform)
{
	if (!IsStrataEnabled())
		return;

	if (FVisualizeMaterialPS::CanRunStrataVizualizeMaterial(Platform))
	{
		RDG_EVENT_SCOPE(GraphBuilder, "StrataVisualizeMaterial");
		for (int32 i = 0; i < Views.Num(); ++i)
		{
			const FViewInfo& View = Views[i];
			AddVisualizeMaterialPasses(GraphBuilder, View, SceneColorTexture, Platform);
		}
	}

	const int32 StrataClassificationDebug = CVarStrataClassificationDebug.GetValueOnAnyThread();
	if (IsClassificationEnabled() && StrataClassificationDebug > 0)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "StrataVisualizeClassification");
		for (int32 i = 0; i < Views.Num(); ++i)
		{
			const FViewInfo& View = Views[i];
			const bool bDebugPass = true;
			AddStrataInternalClassificationTilePass(
				GraphBuilder, View, nullptr, &SceneColorTexture, EStrataTileMaterialType::ESimple, bDebugPass);
			AddStrataInternalClassificationTilePass(
				GraphBuilder, View, nullptr, &SceneColorTexture, EStrataTileMaterialType::EComplex, bDebugPass);
		}
	}

	if (CVarStrataFurnaceTest.GetValueOnAnyThread() > 0)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "StrataVisualizeFurnaceTest");
		for (int32 i = 0; i < Views.Num(); ++i)
		{
			const FViewInfo& View = Views[i];
			AddStrataFurnacePass(GraphBuilder, View, SceneColorTexture);
		}
	}
}

} // namespace Strata


