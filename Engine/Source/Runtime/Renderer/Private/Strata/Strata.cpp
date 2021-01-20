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


IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FStrataGlobalUniformParameters, "Strata");

namespace Strata
{

bool IsStrataEnabled()
{
	return CVarStrata.GetValueOnAnyThread() > 0;
}

void InitialiseStrataFrameSceneData(FSceneRenderer& SceneRenderer, FRDGBuilder& GraphBuilder)
{
	FStrataSceneData& StrataSceneData = SceneRenderer.Scene->StrataSceneData;

	uint32 ResolutionX = 1;
	uint32 ResolutionY = 1;

	if (IsStrataEnabled())
	{
		FIntPoint BufferSizeXY = GetSceneTextureExtent();
		
		ResolutionX = BufferSizeXY.X;
		ResolutionY = BufferSizeXY.Y;

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

		// Top layer texture
		{
			FRDGTextureRef Texture = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(BufferSizeXY, PF_R32G32_UINT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_RenderTargetable), TEXT("StrataTopLayerTexture"));
			GraphBuilder.PreallocateTexture(Texture);
			StrataSceneData.TopLayerTexture = GraphBuilder.GetPooledTexture(Texture);
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
		StrataSceneData.MaterialLobesBuffer.Initialize(DesiredBufferSize, BUF_Static, TEXT("MaterialLobesBuffer"));
	}

	// Set reference to the Strata data from each view
	for (int32 ViewIndex = 0; ViewIndex < SceneRenderer.Views.Num(); ViewIndex++)
	{
		FViewInfo& View = SceneRenderer.Views[ViewIndex];
		View.StrataSceneData = &SceneRenderer.Scene->StrataSceneData;
	}

	// Always reset the strata uniform buffer
	SceneRenderer.Scene->StrataSceneData.StrataGlobalUniformParameters.SafeRelease();
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
		StrataUniformParameters.TopLayerTexture = View.StrataSceneData->TopLayerTexture->GetRenderTargetItem().ShaderResourceTexture;

		View.StrataSceneData->StrataGlobalUniformParameters = CreateUniformBufferImmediate(StrataUniformParameters, UniformBuffer_SingleFrame);
		return View.StrataSceneData->StrataGlobalUniformParameters;
	}
	else
	{
		// Create each time. This path will go away when Strata is always enabled anyway.
		StrataUniformParameters.MaxBytesPerPixel = 0;
		StrataUniformParameters.MaterialLobesBuffer = GEmptyVertexBufferWithUAV->ShaderResourceViewRHI;
		StrataUniformParameters.ClassificationTexture = GSystemTextures.BlackDummy->GetRenderTargetItem().ShaderResourceTexture;
		StrataUniformParameters.TopLayerTexture = GSystemTextures.BlackDummy->GetRenderTargetItem().ShaderResourceTexture;
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

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return GetMaxSupportedFeatureLevel(Parameters.Platform) >= ERHIFeatureLevel::SM5 && Strata::IsStrataEnabled();
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		// Stay debug and skip optimizations to reduce compilation time on this long shader.
		OutEnvironment.CompilerFlags.Add(CFLAG_Debug);
	}
};
IMPLEMENT_GLOBAL_SHADER(FVisualizeMaterialPS, "/Engine/Private/Strata/StrataVisualize.usf", "VisualizeMaterialPS", SF_Pixel);

void AddVisualizeMaterialPasses(FRDGBuilder& GraphBuilder, const TArray<FViewInfo>& Views, FRDGTextureRef SceneColorTexture)
{
	RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, IsStrataEnabled() && Views.Num() > 0, "StrataVisualizeMaterial");
	if (!IsStrataEnabled())
	{
		return;
	}

	FRHIBlendState* PreMultipliedColorTransmittanceBlend = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_SourceAlpha, BO_Add, BF_Zero, BF_One>::GetRHI();

	for (int32 i = 0; i < Views.Num(); ++i)
	{
		const FViewInfo& View = Views[i];

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
	}
};
IMPLEMENT_GLOBAL_SHADER(FStrataMaterialClassificationPassPS, "/Engine/Private/Strata/StrataMaterialClassification.usf", "MainPS", SF_Pixel);

void AddStrataMaterialClassificationPass(FRDGBuilder& GraphBuilder, const TArray<FViewInfo>& Views)
{
	RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, IsStrataEnabled() && Views.Num() > 0, "StrataMaterialClassification");
	if (!IsStrataEnabled())
	{
		return;
	}

	for (int32 i = 0; i < Views.Num(); ++i)
	{
		const FViewInfo& View = Views[i];
		
		FStrataMaterialClassificationPassPS::FPermutationDomain PermutationVector;
		TShaderMapRef<FStrataMaterialClassificationPassPS> PixelShader(View.ShaderMap, PermutationVector);
		FStrataMaterialClassificationPassPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FStrataMaterialClassificationPassPS::FParameters>();
		PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
		PassParameters->Strata = Strata::BindStrataGlobalUniformParameters(View);
		PassParameters->SceneTextures = GetSceneTextureParameters(GraphBuilder);		
		PassParameters->RenderTargets[0] = FRenderTargetBinding(GraphBuilder.RegisterExternalTexture(View.StrataSceneData->ClassificationTexture), ERenderTargetLoadAction::EClear);
		PassParameters->RenderTargets[1] = FRenderTargetBinding(GraphBuilder.RegisterExternalTexture(View.StrataSceneData->TopLayerTexture), ERenderTargetLoadAction::EClear);

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
}


} // namespace Strata


