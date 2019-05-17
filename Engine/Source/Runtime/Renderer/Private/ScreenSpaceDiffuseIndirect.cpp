// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "StaticBoundShaderState.h"
#include "SceneUtils.h"
#include "PostProcess/SceneRenderTargets.h"
#include "SceneRenderTargetParameters.h"
#include "ScenePrivate.h"
#include "PostProcessing.h"
#include "PostProcess/SceneFilterRendering.h"
#include "ClearQuad.h"
#include "PipelineStateCache.h"

static TAutoConsoleVariable<int32> CVarSSGIQuality(
	TEXT("r.SSGI.Quality"),
	0,
	TEXT("Whether to use screen space diffuse indirect and at what quality setting.\n"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

DECLARE_GPU_STAT_NAMED(ScreenSpaceDiffuseIndirect, TEXT("Screen Space Diffuse Indirect"));

bool ShouldRenderScreenSpaceDiffuseIndirect( const FViewInfo& View )
{
	int Quality = CVarSSGIQuality.GetValueOnRenderThread();

	if( Quality <= 0 )
	{
		return false;
	}

	if (IsAnyForwardShadingEnabled(View.GetShaderPlatform()))
	{
		return false;
	}

	return true;
}


class FScreenSpaceDiffuseIndirectPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenSpaceDiffuseIndirectPS);
	SHADER_USE_PARAMETER_STRUCT(FScreenSpaceDiffuseIndirectPS, FGlobalShader)

	class FQualityDim : SHADER_PERMUTATION_INT( "QUALITY", 5 );
	using FPermutationDomain = TShaderPermutationDomain< FQualityDim >;
	
	BEGIN_SHADER_PARAMETER_STRUCT( FParameters, )
		SHADER_PARAMETER( FVector4,		HZBUvFactorAndInvFactor )
		SHADER_PARAMETER( FVector4,		PrevScreenPositionScaleBias )
		SHADER_PARAMETER( float,		PrevSceneColorPreExposureCorrection )
		
		RENDER_TARGET_BINDING_SLOTS()

		SHADER_PARAMETER_RDG_TEXTURE( Texture2D,	HZBTexture )
		SHADER_PARAMETER_SAMPLER( SamplerState,		HZBSampler )

		SHADER_PARAMETER_RDG_TEXTURE( Texture2D,	VelocityTexture )
		SHADER_PARAMETER_SAMPLER( SamplerState,		VelocitySampler )

		SHADER_PARAMETER_RDG_TEXTURE( Texture2D,	ColorTexture )
		SHADER_PARAMETER_SAMPLER( SamplerState,		ColorSampler )

		SHADER_PARAMETER_STRUCT_REF(	FViewUniformShaderParameters,	View )
		SHADER_PARAMETER_STRUCT_REF(	FSceneTexturesUniformParameters,SceneTextures )
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM4);
	}
};

IMPLEMENT_GLOBAL_SHADER( FScreenSpaceDiffuseIndirectPS, "/Engine/Private/ScreenSpaceDiffuseIndirect.usf", "ScreenSpaceDiffuseIndirectPS", SF_Pixel );

// --------------------------------------------------------

void RenderScreenSpaceDiffuseIndirect( FRHICommandListImmediate& RHICmdList, FViewInfo& View, TRefCountPtr<IPooledRenderTarget>& VelocityRT )
{
	FTemporalAAHistory& TemporalAAHistory = View.PrevViewInfo.TemporalAAHistory;

	if( !ShouldRenderScreenSpaceDiffuseIndirect( View ) || !TemporalAAHistory.IsValid() )
	{
		return;
	}
	
	const int32 Quality = FMath::Clamp( CVarSSGIQuality.GetValueOnRenderThread(), 1, 4 );

	FRDGBuilder GraphBuilder( RHICmdList );

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get( RHICmdList );

	FRDGTexture* SceneColor	= GraphBuilder.RegisterExternalTexture( SceneContext.GetSceneColor() );
	FRDGTexture* ScreenSpaceAO = GraphBuilder.CreateTexture(SceneContext.ScreenSpaceAO->GetDesc(), TEXT("SSRTAO"));
	FRDGTexture* HZBTexture	= GraphBuilder.RegisterExternalTexture( View.HZB );
	FRDGTexture* ColorTexture	= GraphBuilder.RegisterExternalTexture( TemporalAAHistory.RT[0] );

	FRDGTexture* VelocityTexture;
	if( VelocityRT && !View.bCameraCut )
	{
		VelocityTexture = GraphBuilder.RegisterExternalTexture( VelocityRT );
	}
	else
	{
		// No velocity, use black
		VelocityTexture = GraphBuilder.RegisterExternalTexture( GSystemTextures.BlackDummy );
	}

	FScreenSpaceDiffuseIndirectPS::FParameters* PassParameters = GraphBuilder.AllocParameters< FScreenSpaceDiffuseIndirectPS::FParameters >();

	PassParameters->RenderTargets[0] = FRenderTargetBinding( SceneColor, ERenderTargetLoadAction::ELoad, ERenderTargetStoreAction::EStore );
	PassParameters->RenderTargets[1] = FRenderTargetBinding( ScreenSpaceAO, ERenderTargetLoadAction::ENoAction, ERenderTargetStoreAction::EStore );

	PassParameters->HZBTexture = HZBTexture;
	PassParameters->HZBSampler = TStaticSamplerState< SF_Point, AM_Clamp, AM_Clamp, AM_Clamp >::GetRHI();

	PassParameters->VelocityTexture = VelocityTexture;
	PassParameters->VelocitySampler = TStaticSamplerState< SF_Point, AM_Clamp, AM_Clamp, AM_Clamp >::GetRHI();

	PassParameters->ColorTexture = ColorTexture;
	PassParameters->ColorSampler = TStaticSamplerState< SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp >::GetRHI();

	const FVector2D HZBUvFactor(
		float( View.ViewRect.Width() )  / float( 2 * View.HZBMipmap0Size.X ),
		float( View.ViewRect.Height() ) / float( 2 * View.HZBMipmap0Size.Y )
		);
			
	PassParameters->HZBUvFactorAndInvFactor = FVector4(
		HZBUvFactor.X,
		HZBUvFactor.Y,
		1.0f / HZBUvFactor.X,
		1.0f / HZBUvFactor.Y );

	FIntPoint ViewportOffset	= TemporalAAHistory.ViewportRect.Min;
	FIntPoint ViewportExtent	= TemporalAAHistory.ViewportRect.Size();
	FIntPoint BufferSize		= TemporalAAHistory.ReferenceBufferSize;

	PassParameters->PrevScreenPositionScaleBias = FVector4(
		 ViewportExtent.X * 0.5f / BufferSize.X,
		-ViewportExtent.Y * 0.5f / BufferSize.Y,
		(ViewportExtent.X * 0.5f + ViewportOffset.X) / BufferSize.X,
		(ViewportExtent.Y * 0.5f + ViewportOffset.Y) / BufferSize.Y );

	PassParameters->PrevSceneColorPreExposureCorrection = View.PreExposure / View.PrevViewInfo.SceneColorPreExposure;

	PassParameters->View = View.ViewUniformBuffer;
	PassParameters->SceneTextures = CreateSceneTextureUniformBufferSingleDraw( RHICmdList, ESceneTextureSetupMode::SceneDepth | ESceneTextureSetupMode::GBuffers, View.FeatureLevel );

	GraphBuilder.AddPass(
		RDG_EVENT_NAME( "ScreenSpaceDiffuseIndirect(Quality=%d) %dx%d", Quality, View.ViewRect.Width(), View.ViewRect.Height() ),
		PassParameters,
		ERenderGraphPassFlags::None,
		[ PassParameters, &View, Quality ]( FRHICommandListImmediate& InRHICmdList )
		{
			SCOPED_GPU_STAT(InRHICmdList, ScreenSpaceDiffuseIndirect);

			InRHICmdList.SetViewport( View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f );

			FScreenSpaceDiffuseIndirectPS::FPermutationDomain PermutationVector;
			PermutationVector.Set< FScreenSpaceDiffuseIndirectPS::FQualityDim >( Quality );

			auto VertexShader	= View.ShaderMap->GetShader< FPostProcessVS >();
			auto PixelShader	= View.ShaderMap->GetShader< FScreenSpaceDiffuseIndirectPS >( PermutationVector );
			CA_ASSUME(PixelShader);

			{
				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				InRHICmdList.ApplyCachedRenderTargets( GraphicsPSOInit );

				GraphicsPSOInit.BlendState = TStaticBlendState< CW_RGB, BO_Add, BF_One, BF_SourceAlpha >::GetRHI();
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
				GraphicsPSOInit.PrimitiveType = PT_TriangleList;
				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX( VertexShader );
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL( PixelShader );

				SetGraphicsPipelineState( InRHICmdList, GraphicsPSOInit );
			}

			SetShaderParameters( InRHICmdList, PixelShader, PixelShader->GetPixelShader(), *PassParameters );

			DrawPostProcessPass(
				InRHICmdList,
				0, 0,
				View.ViewRect.Width(), View.ViewRect.Height(),
				View.ViewRect.Min.X, View.ViewRect.Min.Y,
				View.ViewRect.Width(), View.ViewRect.Height(),
				View.ViewRect.Size(),
				FSceneRenderTargets::Get( InRHICmdList ).GetBufferSizeXY(),
				VertexShader,
				View.StereoPass,
				false,
				EDRF_UseTriangleOptimization);
		});

	GraphBuilder.QueueTextureExtraction(ScreenSpaceAO, &SceneContext.ScreenSpaceAO);

	GraphBuilder.Execute();
}
