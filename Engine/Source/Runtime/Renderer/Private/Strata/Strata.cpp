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
	TEXT("Enables Strata."),
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
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get();
		FIntPoint BufferSizeXY = SceneContext.GetBufferSizeXY();
		
		ResolutionX = BufferSizeXY.X;
		ResolutionY = BufferSizeXY.Y;

		// Previous GBuffer when complete was 28bytes
		// check out Strata.ush to see how this is computed
		const uint32 MaterialConservativeByteCountPerPixel = 97u;
		const uint32 RoundToValue = 4u;
		StrataSceneData.MaxBytesPerPixel = FMath::DivideAndRoundUp(MaterialConservativeByteCountPerPixel, RoundToValue) * RoundToValue;
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

		View.StrataSceneData->StrataGlobalUniformParameters = CreateUniformBufferImmediate(StrataUniformParameters, UniformBuffer_SingleFrame);
		return View.StrataSceneData->StrataGlobalUniformParameters;
	}
	else
	{
		// Create each time. This path will go away when Strata is always enabled anyway.
		StrataUniformParameters.MaxBytesPerPixel = 0;
		StrataUniformParameters.MaterialLobesBuffer = GEmptyVertexBufferWithUAV->ShaderResourceViewRHI;
		return CreateUniformBufferImmediate(StrataUniformParameters, UniformBuffer_SingleDraw);
	}
}


////////////////////////////////////////////////////////////////////////// Debug


class FVisualizeMaterialPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVisualizeMaterialPS);
	SHADER_USE_PARAMETER_STRUCT(FVisualizeMaterialPS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_STRUCT_REF(FStrataGlobalUniformParameters, Strata)
		SHADER_PARAMETER_TEXTURE(Texture2D, MiniFontTexture)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
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

		FVisualizeMaterialPS::FPermutationDomain PermutationVector;
		TShaderMapRef<FVisualizeMaterialPS> PixelShader(View.ShaderMap, PermutationVector);

		if (View.Family->EngineShowFlags.VisualizeStrataMaterial)
		{
			FVisualizeMaterialPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVisualizeMaterialPS::FParameters>();
			PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
			PassParameters->Strata = Strata::BindStrataGlobalUniformParameters(View);
			PassParameters->MiniFontTexture = GetMiniFontTexture();
			PassParameters->SceneTextures = GetSceneTextureParameters(GraphBuilder);
			PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneColorTexture, ERenderTargetLoadAction::ELoad);

			FPixelShaderUtils::AddFullscreenPass<FVisualizeMaterialPS>(GraphBuilder, View.ShaderMap, RDG_EVENT_NAME("StrataVisualizeMaterial"),
				PixelShader, PassParameters, View.ViewRect, PreMultipliedColorTransmittanceBlend);
		}
	}
}


} // namespace Strata


