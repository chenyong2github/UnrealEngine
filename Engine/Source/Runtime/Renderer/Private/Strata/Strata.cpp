// Copyright Epic Games, Inc. All Rights Reserved.

#include "Strata.h"
#include "HAL/IConsoleManager.h"
#include "PixelShaderUtils.h"
#include "SceneView.h"
#include "ScenePrivate.h"
#include "SceneRendering.h"
#include "RendererInterface.h"
#include "UniformBuffer.h"



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
	FStrataData& StrataData = SceneRenderer.Scene->StrataData;

	uint32 ResolutionX = 1;
	uint32 ResolutionY = 1;

	if (IsStrataEnabled())
	{
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);
		FIntPoint BufferSizeXY = SceneContext.GetBufferSizeXY();
		
		ResolutionX = BufferSizeXY.X;
		ResolutionY = BufferSizeXY.Y;

		// Previous GBuffer when complete was 28bytes
		// check out Strata.ush to see how this is computed
		const uint32 MaterialConservativeByteCountPerPixel = 129u;
		StrataData.MaxBytesPerPixel = FMath::DivideAndRoundUp(MaterialConservativeByteCountPerPixel, 4u) * 4u;
	}
	else
	{
		StrataData.MaxBytesPerPixel = 4u;
	}

	FRDGTextureRef MaterialLobesTexture = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2D(FIntPoint(ResolutionX, ResolutionY), PF_R16F, FClearValueBinding::None,
		TexCreate_RenderTargetable | TexCreate_ShaderResource | TexCreate_UAV), TEXT("MaterialLobesTexture"));
	AddClearRenderTargetPass(GraphBuilder, MaterialLobesTexture, FLinearColor::Black);
	ConvertToExternalTexture(GraphBuilder, MaterialLobesTexture, StrataData.MaterialLobesTexture);

	const uint32 DesiredBufferSize = FMath::Max(4u, ResolutionX * ResolutionY * StrataData.MaxBytesPerPixel);
	if (StrataData.MaterialLobesBuffer.NumBytes < DesiredBufferSize)
	{
		if (StrataData.MaterialLobesBuffer.NumBytes > 0)
		{
			StrataData.MaterialLobesBuffer.Release();
		}
		StrataData.MaterialLobesBuffer.Initialize(DesiredBufferSize, BUF_Static, TEXT("MaterialLobesBuffer"));
	}

	// Set reference to the Strata data from each view
	for (int32 ViewIndex = 0; ViewIndex < SceneRenderer.Views.Num(); ViewIndex++)
	{
		FViewInfo& View = SceneRenderer.Views[ViewIndex];
		View.StrataData = &SceneRenderer.Scene->StrataData;
	}
}

void BindStrataBasePassUniformParameters(const FViewInfo& View, FStrataOpaquePassUniformParameters& OutStrataUniformParameters)
{
	if (View.StrataData)
	{
		OutStrataUniformParameters.MaxBytesPerPixel = View.StrataData->MaxBytesPerPixel;
		OutStrataUniformParameters.MaterialLobesTextureUAV = View.StrataData->MaterialLobesTexture->GetRenderTargetItem().UAV;
		OutStrataUniformParameters.MaterialLobesBufferUAV = View.StrataData->MaterialLobesBuffer.UAV;
	}
	else
	{
		OutStrataUniformParameters.MaxBytesPerPixel = 0;
		OutStrataUniformParameters.MaterialLobesTextureUAV = GEmptyVertexBufferWithUAV->UnorderedAccessViewRHI;
		OutStrataUniformParameters.MaterialLobesBufferUAV = GEmptyVertexBufferWithUAV->UnorderedAccessViewRHI;
	}
}

TUniformBufferRef<FStrataGlobalUniformParameters> BindStrataGlobalUniformParameters(const FViewInfo& View)
{
	FStrataGlobalUniformParameters StrataUniformParameters;
	if (View.StrataData)
	{
		StrataUniformParameters.MaxBytesPerPixel = View.StrataData->MaxBytesPerPixel;
		StrataUniformParameters.MaterialLobesTexture = View.StrataData->MaterialLobesTexture->GetRenderTargetItem().ShaderResourceTexture;
		StrataUniformParameters.MaterialLobesBuffer = View.StrataData->MaterialLobesBuffer.SRV;
	}
	else
	{
		StrataUniformParameters.MaxBytesPerPixel = 0;
		StrataUniformParameters.MaterialLobesTexture = GSystemTextures.BlackDummy->GetRenderTargetItem().ShaderResourceTexture;
		StrataUniformParameters.MaterialLobesBuffer = GEmptyVertexBufferWithUAV->ShaderResourceViewRHI;
	}

	// STRATA_TODO cache this on the view and use UniformBuffer_SingleFrame
	return CreateUniformBufferImmediate(StrataUniformParameters, UniformBuffer_SingleDraw);
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
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return GetMaxSupportedFeatureLevel(Parameters.Platform) >= ERHIFeatureLevel::SM5;
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
			PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneColorTexture, ERenderTargetLoadAction::ELoad);

			FPixelShaderUtils::AddFullscreenPass<FVisualizeMaterialPS>(GraphBuilder, View.ShaderMap, RDG_EVENT_NAME("StrataVisualizeMaterial"),
				PixelShader, PassParameters, View.ViewRect, PreMultipliedColorTransmittanceBlend);
		}
	}
}


} // namespace Strata


