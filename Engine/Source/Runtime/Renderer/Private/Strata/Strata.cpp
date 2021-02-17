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
	TEXT("Enable strata classification visualization."),
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
	TEXT("Enable Strata furnace test (for debug purpose) 1: roughness/metallic 2: a selection of conductors."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarStrataFurnaceTestSampleCount(
	TEXT("r.Strata.FurnaceTest.SampleCount"),
	1024,
	TEXT("Number of sample used for furnace test."),
	ECVF_RenderThreadSafe);

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FStrataGlobalUniformParameters, "Strata");

namespace Strata
{

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

uint32 GetStrataTileSize()
{
	return 8;
}

static void AddStrataLUTPass(FRDGBuilder& GraphBuilder, const FViewInfo& View);

void InitialiseStrataFrameSceneData(FSceneRenderer& SceneRenderer, FRDGBuilder& GraphBuilder)
{
	FStrataSceneData& StrataSceneData = SceneRenderer.Scene->StrataSceneData;

	uint32 ResolutionX = 1;
	uint32 ResolutionY = 1;
	bool bUpdateLUT = false;
	if (IsStrataEnabled())
	{
		FIntPoint BufferSizeXY = GetSceneTextureExtent();
		
		// We need to allocate enough for the tiled memory addressing to always work
		ResolutionX = FMath::DivideAndRoundUp(BufferSizeXY.X, STRATA_DATA_TILE_SIZE) * STRATA_DATA_TILE_SIZE;
		ResolutionY = FMath::DivideAndRoundUp(BufferSizeXY.Y, STRATA_DATA_TILE_SIZE) * STRATA_DATA_TILE_SIZE;

		// Previous GBuffer when complete was 28bytes
		// check out Strata.ush to see how this is computed
		const uint32 MaterialConservativeByteCountPerPixel = 100u;
		const uint32 RoundToValue = 4u;
		StrataSceneData.MaxBytesPerPixel = FMath::DivideAndRoundUp(MaterialConservativeByteCountPerPixel, RoundToValue) * RoundToValue;

		// Classification texture
		{
			FRDGTextureRef Texture = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(BufferSizeXY, PF_R32_UINT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_RenderTargetable), TEXT("StrataClassificationTexture"));
			GraphBuilder.PreallocateTexture(Texture);
			StrataSceneData.ClassificationTexture = GraphBuilder.GetPooledTexture(Texture);
		}

		// Tile classification buffers
		{
			const int32 TileInPixel = GetStrataTileSize();
			const FIntPoint TileResolution(FMath::DivideAndRoundUp(BufferSizeXY.X, TileInPixel), FMath::DivideAndRoundUp(BufferSizeXY.Y, TileInPixel));
			FRDGBufferRef ClassificationTileListBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), TileResolution.X * TileResolution.Y), TEXT("StrataTileListBuffer"));
			FRDGBufferRef ClassificationTileIndirectBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDrawIndirectParameters>(), TEXT("StrataTileIndirectBuffer")); 

			GraphBuilder.PreallocateBuffer(ClassificationTileListBuffer);
			GraphBuilder.PreallocateBuffer(ClassificationTileIndirectBuffer);
			StrataSceneData.ClassificationTileListBuffer = GraphBuilder.GetPooledBuffer(ClassificationTileListBuffer);
			StrataSceneData.ClassificationTileIndirectBuffer = GraphBuilder.GetPooledBuffer(ClassificationTileIndirectBuffer);

			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalBuffer(StrataSceneData.ClassificationTileIndirectBuffer), PF_R32_UINT), 0);
		}

		// Top layer texture
		{
			FRDGTextureRef Texture = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(BufferSizeXY, PF_R32_UINT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_RenderTargetable), TEXT("StrataTopLayerNormalTexture"));
			GraphBuilder.PreallocateTexture(Texture);
			StrataSceneData.TopLayerNormalTexture = GraphBuilder.GetPooledTexture(Texture);
		}

		// SSS texture
		{
			FRDGTextureRef Texture = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(BufferSizeXY, PF_R32G32_UINT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_RenderTargetable), TEXT("StrataSSSTexture"));
			GraphBuilder.PreallocateTexture(Texture);
			StrataSceneData.SSSTexture = GraphBuilder.GetPooledTexture(Texture);
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

			GraphBuilder.PreallocateTexture(OutLUT3D);
			GraphBuilder.PreallocateTexture(OutLUT2D);
			StrataSceneData.GGXEnergyLUT3DTexture = GraphBuilder.GetPooledTexture(OutLUT3D);
			StrataSceneData.GGXEnergyLUT2DTexture = GraphBuilder.GetPooledTexture(OutLUT2D);
		}
	}
	else
	{
		StrataSceneData.MaxBytesPerPixel = 4u;
	}

	const uint32 DesiredBufferSize = FMath::Max(4u, ResolutionX * ResolutionY * StrataSceneData.MaxBytesPerPixel);
	if (StrataSceneData.MaterialLobesBuffer.NumBytes < DesiredBufferSize)
	{
		if (StrataSceneData.MaterialLobesBuffer.NumBytes > 0)
		{
			StrataSceneData.MaterialLobesBuffer.Release();
		}
		StrataSceneData.MaterialLobesBuffer.Initialize(TEXT("MaterialLobesBuffer"), DesiredBufferSize, BUF_Static);
	}

	// Set reference to the Strata data from each view
	for (int32 ViewIndex = 0; ViewIndex < SceneRenderer.Views.Num(); ViewIndex++)
	{
		FViewInfo& View = SceneRenderer.Views[ViewIndex];
		View.StrataSceneData = &SceneRenderer.Scene->StrataSceneData;
	}

	// Always reset the strata uniform buffer
	SceneRenderer.Scene->StrataSceneData.StrataGlobalUniformParameters.SafeRelease();

	AddStrataClearMaterialBufferPass(GraphBuilder, StrataSceneData.MaterialLobesBuffer.UAV, StrataSceneData.MaxBytesPerPixel, FIntPoint(ResolutionX, ResolutionY));

	if (bUpdateLUT)
	{
		for (int32 ViewIndex = 0; ViewIndex < SceneRenderer.Views.Num(); ViewIndex++)
		{
			FViewInfo& View = SceneRenderer.Views[ViewIndex];
			AddStrataLUTPass(GraphBuilder, View);
		}
	}
}

void BindStrataBasePassUniformParameters(const FViewInfo& View, FStrataOpaquePassUniformParameters& OutStrataUniformParameters)
{
	if (View.StrataSceneData)
	{
		OutStrataUniformParameters.MaxBytesPerPixel = View.StrataSceneData->MaxBytesPerPixel;
		OutStrataUniformParameters.MaterialLobesBufferUAV = View.StrataSceneData->MaterialLobesBuffer.UAV;
	}
	else
	{
		OutStrataUniformParameters.MaxBytesPerPixel = 0;
		OutStrataUniformParameters.MaterialLobesBufferUAV = GEmptyVertexBufferWithUAV->UnorderedAccessViewRHI;
	}
}

TUniformBufferRef<FStrataGlobalUniformParameters> BindStrataGlobalUniformParameters(const FViewInfo& View)
{
	// If the strata scene data has not been created this frame yet, create it.
	FStrataGlobalUniformParameters StrataUniformParameters;
	if (View.StrataSceneData)
	{
		if (View.StrataSceneData->StrataGlobalUniformParameters.IsValid())
		{
			return View.StrataSceneData->StrataGlobalUniformParameters;
		}

		StrataUniformParameters.MaxBytesPerPixel = View.StrataSceneData->MaxBytesPerPixel;
		StrataUniformParameters.MaterialLobesBuffer = View.StrataSceneData->MaterialLobesBuffer.SRV;
		StrataUniformParameters.ClassificationTexture = View.StrataSceneData->ClassificationTexture->GetRenderTargetItem().ShaderResourceTexture;
		StrataUniformParameters.TopLayerNormalTexture = View.StrataSceneData->TopLayerNormalTexture->GetRenderTargetItem().ShaderResourceTexture;
		StrataUniformParameters.GGXEnergyLUT3DTexture = View.StrataSceneData->GGXEnergyLUT3DTexture->GetRenderTargetItem().ShaderResourceTexture;
		StrataUniformParameters.GGXEnergyLUT2DTexture = View.StrataSceneData->GGXEnergyLUT2DTexture->GetRenderTargetItem().ShaderResourceTexture;
		StrataUniformParameters.GGXEnergyLUTSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		StrataUniformParameters.GGXEnergyLUTScaleBias = GetStrataGGXEnergyLUTScaleBias();
		StrataUniformParameters.SSSTexture = View.StrataSceneData->SSSTexture->GetRenderTargetItem().ShaderResourceTexture;
		View.StrataSceneData->StrataGlobalUniformParameters = CreateUniformBufferImmediate(StrataUniformParameters, UniformBuffer_SingleFrame);
		return View.StrataSceneData->StrataGlobalUniformParameters;
	}
	else
	{
		// Create each time. This path will go away when Strata is always enabled anyway.
		StrataUniformParameters.MaxBytesPerPixel = 0;
		StrataUniformParameters.MaterialLobesBuffer = GEmptyVertexBufferWithUAV->ShaderResourceViewRHI;
		StrataUniformParameters.ClassificationTexture = GSystemTextures.ZeroUIntDummy->GetRenderTargetItem().ShaderResourceTexture;
		StrataUniformParameters.TopLayerNormalTexture = GSystemTextures.ZeroUIntDummy->GetRenderTargetItem().ShaderResourceTexture;
		StrataUniformParameters.SSSTexture = GSystemTextures.ZeroUIntDummy->GetRenderTargetItem().ShaderResourceTexture;
		StrataUniformParameters.GGXEnergyLUT3DTexture = GSystemTextures.VolumetricBlackDummy->GetRenderTargetItem().ShaderResourceTexture;
		StrataUniformParameters.GGXEnergyLUT2DTexture = GSystemTextures.BlackDummy->GetRenderTargetItem().ShaderResourceTexture;
		StrataUniformParameters.GGXEnergyLUTSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		StrataUniformParameters.GGXEnergyLUTScaleBias = GetStrataGGXEnergyLUTScaleBias();
		return CreateUniformBufferImmediate(StrataUniformParameters, UniformBuffer_SingleDraw);
	}
}

////////////////////////////////////////////////////////////////////////// 
// Debug

class FVisualizeMaterialPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVisualizeMaterialPS);
	SHADER_USE_PARAMETER_STRUCT(FVisualizeMaterialPS, FGlobalShader);

	class FBSDFPass : SHADER_PERMUTATION_INT("PERMUTATION_BSDF_PASS", 4);
	using FPermutationDomain = TShaderPermutationDomain<FBSDFPass>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_STRUCT_REF(FStrataGlobalUniformParameters, Strata)
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
		PassParameters->Strata = Strata::BindStrataGlobalUniformParameters(View);
		PassParameters->MiniFontTexture = GetMiniFontTexture();
		PassParameters->SceneTextures = GetSceneTextureParameters(GraphBuilder);
		PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneColorTexture, ERenderTargetLoadAction::ELoad);

		if (ShaderDrawDebug::IsShaderDrawDebugEnabled())
		{
			ShaderDrawDebug::SetParameters(GraphBuilder, View.ShaderDrawData, PassParameters->ShaderDrawParameters);
		}

		for (uint32 j = 0; j < 4; ++j)
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
		SHADER_PARAMETER_STRUCT_REF(FStrataGlobalUniformParameters, Strata)
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
		SHADER_PARAMETER_UAV(RWByteAddressBuffer, MaterialLobesBufferUAV)
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
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, TileIndirectData)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, TileListData)
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


class FStrataMaterialStencilClassificationPassVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FStrataMaterialStencilClassificationPassVS);
	SHADER_USE_PARAMETER_STRUCT(FStrataMaterialStencilClassificationPassVS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER(int32, TileSize)
		SHADER_PARAMETER(int32, bRectPrimitive)
		SHADER_PARAMETER(FIntPoint, TileCount)
		SHADER_PARAMETER(FIntPoint, OutputResolution)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, TileListBuffer)
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

class FStrataMaterialStencilClassificationPassPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FStrataMaterialStencilClassificationPassPS);
	SHADER_USE_PARAMETER_STRUCT(FStrataMaterialStencilClassificationPassPS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER(int32, TileSize)
		SHADER_PARAMETER(int32, bRectPrimitive)
		SHADER_PARAMETER(FIntPoint, TileCount)
		SHADER_PARAMETER(FIntPoint, OutputResolution)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, TileListBuffer)
		SHADER_PARAMETER_RDG_BUFFER(Buffer, TileIndirectBuffer)
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

IMPLEMENT_GLOBAL_SHADER(FStrataMaterialStencilClassificationPassVS, "/Engine/Private/Strata/StrataMaterialClassification.usf", "StencilMainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FStrataMaterialStencilClassificationPassPS, "/Engine/Private/Strata/StrataMaterialClassification.usf", "StencilMainPS", SF_Pixel);

static void AddStrataInternalClassifedTilePass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FRDGTextureRef* DepthTexture,
	const FRDGTextureRef* ColorTexture,
	FRDGBufferRef TileListBuffer,
	FRDGBufferRef TileIndirectBuffer)
{	
	const FIntPoint OutputResolution = View.ViewRect.Size();
	const int32 TileSize = GetStrataTileSize();
	
	const FIntPoint TileCount(FMath::DivideAndRoundUp(OutputResolution.X, TileSize), FMath::DivideAndRoundUp(OutputResolution.Y, TileSize));
	const uint32 InstanceCount = TileCount.X * TileCount.Y;

	FStrataMaterialStencilClassificationPassPS::FParameters* ParametersPS = GraphBuilder.AllocParameters<FStrataMaterialStencilClassificationPassPS::FParameters>();
	ParametersPS->TileSize = TileSize;
	ParametersPS->TileCount = TileCount;
	ParametersPS->bRectPrimitive = GRHISupportsRectTopology ? 1 : 0;
	ParametersPS->OutputResolution = OutputResolution;
	ParametersPS->TileListBuffer = GraphBuilder.CreateSRV(TileListBuffer, PF_R32_UINT);
	ParametersPS->TileIndirectBuffer = TileIndirectBuffer;

	TShaderMapRef<FStrataMaterialStencilClassificationPassVS> VertexShader(View.ShaderMap);
	TShaderMapRef<FStrataMaterialStencilClassificationPassPS> PixelShader(View.ShaderMap);

	// For debug purpose
	const bool bDebug = ColorTexture != nullptr;
	if (bDebug)
	{
		ParametersPS->RenderTargets[0] = FRenderTargetBinding(*ColorTexture, ERenderTargetLoadAction::ELoad);
	}
	else
	{
		check(DepthTexture);
		ParametersPS->RenderTargets.DepthStencil = FDepthStencilBinding(
			*DepthTexture,
			ERenderTargetLoadAction::ELoad,
			ERenderTargetLoadAction::ELoad,
			FExclusiveDepthStencil::DepthNop_StencilWrite);
	}
	
	GraphBuilder.AddPass(
		bDebug ? 
		RDG_EVENT_NAME("StrataDebugClassificationPass") :
		RDG_EVENT_NAME("StrataStencilClassificationPass"),
		ParametersPS,
		ERDGPassFlags::Raster,
		[ParametersPS, VertexShader, PixelShader, OutputResolution, InstanceCount, bDebug](FRHICommandList& RHICmdList)
		{
			FStrataMaterialStencilClassificationPassVS::FParameters ParametersVS;
			ParametersVS.TileSize = ParametersPS->TileSize;
			ParametersVS.bRectPrimitive = ParametersPS->bRectPrimitive;
			ParametersVS.TileCount = ParametersPS->TileCount;
			ParametersVS.OutputResolution = ParametersPS->OutputResolution;
			ParametersVS.TileListBuffer = ParametersPS->TileListBuffer;

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Max, BF_SourceAlpha, BF_DestAlpha>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			if (bDebug)
			{
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			}
			else
			{
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<
					false, CF_Always, 
					true,  CF_Always, SO_Keep, SO_Keep, SO_Replace,
					false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
					0xFF, StencilBit>::GetRHI();
			}
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = ParametersPS->bRectPrimitive > 0 ? PT_RectList : PT_TriangleList;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
			SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), ParametersVS);
			//SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *ParametersPS);

			RHICmdList.SetStencilRef(StencilBit);
			RHICmdList.SetViewport(0, 0, 0.0f, OutputResolution.X, OutputResolution.Y, 1.0f);
			RHICmdList.SetStreamSource(0, nullptr, 0);
			RHICmdList.DrawPrimitiveIndirect(ParametersPS->TileIndirectBuffer->GetRHI(), 0);
		});
}

void AddStrataStencilPass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FMinimalSceneTextures& SceneTextures)
{
	FRDGBufferRef TileListBuffer = GraphBuilder.RegisterExternalBuffer(View.StrataSceneData->ClassificationTileListBuffer);
	FRDGBufferRef TileIndirectBuffer = GraphBuilder.RegisterExternalBuffer(View.StrataSceneData->ClassificationTileIndirectBuffer);
	AddStrataInternalClassifedTilePass(GraphBuilder, View, &SceneTextures.Depth.Target, nullptr, TileListBuffer, TileIndirectBuffer);
}

void AddStrataStencilPass(
	FRDGBuilder& GraphBuilder,
	const TArray<FViewInfo>& Views,
	const FMinimalSceneTextures& SceneTextures)
{
	for (int32 i = 0; i < Views.Num(); ++i)
	{
		const FViewInfo& View = Views[i];
		FRDGBufferRef TileListBuffer = GraphBuilder.RegisterExternalBuffer(View.StrataSceneData->ClassificationTileListBuffer);
		FRDGBufferRef TileIndirectBuffer = GraphBuilder.RegisterExternalBuffer(View.StrataSceneData->ClassificationTileIndirectBuffer);
		AddStrataInternalClassifedTilePass(GraphBuilder, View, &SceneTextures.Depth.Target, nullptr, TileListBuffer, TileIndirectBuffer);
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
		FRDGTextureRef ClassificationTexture = GraphBuilder.RegisterExternalTexture(View.StrataSceneData->ClassificationTexture);
		{
			FStrataMaterialClassificationPassPS::FPermutationDomain PermutationVector;
			TShaderMapRef<FStrataMaterialClassificationPassPS> PixelShader(View.ShaderMap, PermutationVector);
			FStrataMaterialClassificationPassPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FStrataMaterialClassificationPassPS::FParameters>();
			PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
			PassParameters->Strata = Strata::BindStrataGlobalUniformParameters(View);
			PassParameters->SceneTextures = GetSceneTextureParameters(GraphBuilder);		
			PassParameters->RenderTargets[0] = FRenderTargetBinding(ClassificationTexture, ERenderTargetLoadAction::EClear);
			PassParameters->RenderTargets[1] = FRenderTargetBinding(GraphBuilder.RegisterExternalTexture(View.StrataSceneData->TopLayerNormalTexture), ERenderTargetLoadAction::EClear);
			PassParameters->RenderTargets[2] = FRenderTargetBinding(GraphBuilder.RegisterExternalTexture(View.StrataSceneData->SSSTexture), ERenderTargetLoadAction::EClear);

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
			PassParameters->TileSize = GetStrataTileSize();
			PassParameters->bRectPrimitive = GRHISupportsRectTopology ? 1 : 0;
			PassParameters->ViewResolution = View.ViewRect.Size();
			PassParameters->ClassificationTexture = ClassificationTexture;
			PassParameters->TileListData = GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalBuffer(View.StrataSceneData->ClassificationTileListBuffer), PF_R32_UINT);
			PassParameters->TileIndirectData = GraphBuilder.CreateUAV(GraphBuilder.RegisterExternalBuffer(View.StrataSceneData->ClassificationTileIndirectBuffer), PF_R32_UINT);

			// Add 64 threads permutation
			const uint32 GroupSize = 8;
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("StrataMaterialTileClassification"),
				ComputeShader,
				PassParameters,
				FComputeShaderUtils::GetGroupCount(ClassificationTexture->Desc.Extent, GroupSize));
		}
	}
}

void AddStrataClearMaterialBufferPass(FRDGBuilder& GraphBuilder, FUnorderedAccessViewRHIRef MaterialLobesBufferUAV, uint32 MaxBytesPerPixel, FIntPoint TiledViewBufferResolution)
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
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
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

static void AddStrataLUTPass(FRDGBuilder& GraphBuilder, const FViewInfo& View)
{
	const uint32 LUTResolution = GetStrataGGXEnergyLUTResolution();
	const uint32 SliceResolution = FMath::CeilToInt(FMath::Sqrt(float(LUTResolution)));
	
	FIntPoint OutputResolutionRT;
	OutputResolutionRT.X = LUTResolution * SliceResolution;
	OutputResolutionRT.Y = LUTResolution * SliceResolution;

	FRDGTextureRef OutLUT3D = GraphBuilder.RegisterExternalTexture(View.StrataSceneData->GGXEnergyLUT3DTexture);
	FRDGTextureRef OutLUT2D = GraphBuilder.RegisterExternalTexture(View.StrataSceneData->GGXEnergyLUT2DTexture);

	// For debug purpose	
	FRDGTextureDesc UnfoldLUTDesc = FRDGTextureDesc::Create2D(OutputResolutionRT, EPixelFormat::PF_G16R16F, FClearValueBinding::Black, ETextureCreateFlags::TexCreate_RenderTargetable);
	FRDGTextureRef UnfoldLUTTexture = GraphBuilder.CreateTexture(UnfoldLUTDesc, TEXT("StrataEnergyUnfoldLUT"));

	TShaderMapRef<FStrataLUTPassPS> PixelShader(View.ShaderMap);
	FStrataLUTPassPS::FParameters* Parameters = GraphBuilder.AllocParameters<FStrataLUTPassPS::FParameters>();
	Parameters->ViewUniformBuffer = View.ViewUniformBuffer;
	Parameters->NumSamples = FMath::Clamp(CVarStrataLUTSampleCount.GetValueOnAnyThread(), 16, 2048);
	Parameters->EnergyLUTResolution = LUTResolution;
	Parameters->SliceXYCount = FIntPoint(SliceResolution, SliceResolution);
	Parameters->OutLUT2D = GraphBuilder.CreateUAV(OutLUT2D);
	Parameters->OutLUT3D = GraphBuilder.CreateUAV(OutLUT3D);
	Parameters->RenderTargets[0] = FRenderTargetBinding(UnfoldLUTTexture, ERenderTargetLoadAction::EClear);

	FPixelShaderUtils::AddFullscreenPass<FStrataLUTPassPS>(
		GraphBuilder,
		View.ShaderMap,
		RDG_EVENT_NAME("StrataLUT"),
		PixelShader,
		Parameters,
		View.ViewRect);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FStrataFurnaceTestPassPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FStrataFurnaceTestPassPS);
	SHADER_USE_PARAMETER_STRUCT(FStrataFurnaceTestPassPS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_STRUCT_REF(FStrataGlobalUniformParameters, Strata)
		SHADER_PARAMETER(uint32, NumSamples)
		SHADER_PARAMETER(uint32, SceneType)
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
	Parameters->Strata = Strata::BindStrataGlobalUniformParameters(View);
	Parameters->SceneType = FMath::Clamp(CVarStrataFurnaceTest.GetValueOnAnyThread(), 1, 2);
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

	if (IsClassificationEnabled() && CVarStrataClassificationDebug.GetValueOnAnyThread() > 0)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "StrataVisualizeClassification");
		for (int32 i = 0; i < Views.Num(); ++i)
		{
			const FViewInfo& View = Views[i];
			FRDGBufferRef TileListBuffer = GraphBuilder.RegisterExternalBuffer(View.StrataSceneData->ClassificationTileListBuffer);
			FRDGBufferRef TileIndirectBuffer = GraphBuilder.RegisterExternalBuffer(View.StrataSceneData->ClassificationTileIndirectBuffer);
			AddStrataInternalClassifedTilePass(GraphBuilder, View, nullptr, &SceneColorTexture, TileListBuffer, TileIndirectBuffer);
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


