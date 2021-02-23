// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	DistanceFieldLightingPost.cpp
=============================================================================*/

#include "DistanceFieldLightingPost.h"
#include "PostProcess/PostProcessing.h"
#include "PostProcess/SceneFilterRendering.h"
#include "DistanceFieldLightingShared.h"
#include "DistanceFieldAmbientOcclusion.h"
#include "CompositionLighting/PostProcessAmbientOcclusion.h"
#include "PipelineStateCache.h"

IMPLEMENT_TYPE_LAYOUT(FLightTileIntersectionParameters);

#if WITH_MGPU
DECLARE_GPU_STAT(AFRWaitForDistanceFieldAOHistory);
#endif

int32 GAOUseHistory = 1;
FAutoConsoleVariableRef CVarAOUseHistory(
	TEXT("r.AOUseHistory"),
	GAOUseHistory,
	TEXT("Whether to apply a temporal filter to the distance field AO, which reduces flickering but also adds trails when occluders are moving."),
	ECVF_RenderThreadSafe
	);

int32 GAOClearHistory = 0;
FAutoConsoleVariableRef CVarAOClearHistory(
	TEXT("r.AOClearHistory"),
	GAOClearHistory,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

int32 GAOHistoryStabilityPass = 1;
FAutoConsoleVariableRef CVarAOHistoryStabilityPass(
	TEXT("r.AOHistoryStabilityPass"),
	GAOHistoryStabilityPass,
	TEXT("Whether to gather stable results to fill in holes in the temporal reprojection.  Adds some GPU cost but improves temporal stability with foliage."),
	ECVF_RenderThreadSafe
	);

float GAOHistoryWeight = .85f;
FAutoConsoleVariableRef CVarAOHistoryWeight(
	TEXT("r.AOHistoryWeight"),
	GAOHistoryWeight,
	TEXT("Amount of last frame's AO to lerp into the final result.  Higher values increase stability, lower values have less streaking under occluder movement."),
	ECVF_RenderThreadSafe
	);

float GAOHistoryDistanceThreshold = 30;
FAutoConsoleVariableRef CVarAOHistoryDistanceThreshold(
	TEXT("r.AOHistoryDistanceThreshold"),
	GAOHistoryDistanceThreshold,
	TEXT("World space distance threshold needed to discard last frame's DFAO results.  Lower values reduce ghosting from characters when near a wall but increase flickering artifacts."),
	ECVF_RenderThreadSafe
	);

float GAOViewFadeDistanceScale = .7f;
FAutoConsoleVariableRef CVarAOViewFadeDistanceScale(
	TEXT("r.AOViewFadeDistanceScale"),
	GAOViewFadeDistanceScale,
	TEXT("Distance over which AO will fade out as it approaches r.AOMaxViewDistance, as a fraction of r.AOMaxViewDistance."),
	ECVF_RenderThreadSafe
	);

bool UseAOHistoryStabilityPass()
{
	extern int32 GDistanceFieldAOQuality;
	return GAOHistoryStabilityPass && GDistanceFieldAOQuality >= 2;
}

class FGeometryAwareUpsampleParameters
{
	DECLARE_INLINE_TYPE_LAYOUT(FGeometryAwareUpsampleParameters, NonVirtual);
public:
	void Bind(const FShaderParameterMap& ParameterMap)
	{
		DistanceFieldNormalTexture.Bind(ParameterMap, TEXT("DistanceFieldNormalTexture"));
		DistanceFieldNormalSampler.Bind(ParameterMap, TEXT("DistanceFieldNormalSampler"));
		BentNormalAOTexture.Bind(ParameterMap, TEXT("BentNormalAOTexture"));
		BentNormalAOSampler.Bind(ParameterMap, TEXT("BentNormalAOSampler"));
		DistanceFieldGBufferTexelSize.Bind(ParameterMap, TEXT("DistanceFieldGBufferTexelSize"));
		DistanceFieldGBufferJitterOffset.Bind(ParameterMap, TEXT("DistanceFieldGBufferJitterOffset"));
		BentNormalBufferAndTexelSize.Bind(ParameterMap, TEXT("BentNormalBufferAndTexelSize"));
		MinDownsampleFactorToBaseLevel.Bind(ParameterMap, TEXT("MinDownsampleFactorToBaseLevel"));
		DistanceFadeScale.Bind(ParameterMap, TEXT("DistanceFadeScale"));
		JitterOffset.Bind(ParameterMap, TEXT("JitterOffset"));
	}

	void Set(FRHICommandList& RHICmdList, FRHIPixelShader* ShaderRHI, const FViewInfo& View, FSceneRenderTargetItem& DistanceFieldNormal, FSceneRenderTargetItem& DistanceFieldAOBentNormal)
	{
		SetTextureParameter(
			RHICmdList,
			ShaderRHI,
			DistanceFieldNormalTexture,
			DistanceFieldNormalSampler,
			TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(),
			DistanceFieldNormal.ShaderResourceTexture
		);

		SetTextureParameter(
			RHICmdList,
			ShaderRHI,
			BentNormalAOTexture,
			BentNormalAOSampler,
			TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(),
			DistanceFieldAOBentNormal.ShaderResourceTexture
		);

		extern FVector2D GetJitterOffset(int32 SampleIndex);
		FVector2D const JitterOffsetValue = GetJitterOffset(View.ViewState->GetDistanceFieldTemporalSampleIndex());

		const FIntPoint DownsampledBufferSize = GetBufferSizeForAO();
		const FVector2D BaseLevelTexelSizeValue(1.0f / DownsampledBufferSize.X, 1.0f / DownsampledBufferSize.Y);
		SetShaderValue(RHICmdList, ShaderRHI, DistanceFieldGBufferTexelSize, BaseLevelTexelSizeValue);

		SetShaderValue(RHICmdList, ShaderRHI, DistanceFieldGBufferJitterOffset, BaseLevelTexelSizeValue * JitterOffsetValue);

		extern FIntPoint GetBufferSizeForConeTracing();
		const FIntPoint ConeTracingBufferSize = GetBufferSizeForConeTracing();
		const FVector4 BentNormalBufferAndTexelSizeValue(ConeTracingBufferSize.X, ConeTracingBufferSize.Y, 1.0f / ConeTracingBufferSize.X, 1.0f / ConeTracingBufferSize.Y);
		SetShaderValue(RHICmdList, ShaderRHI, BentNormalBufferAndTexelSize, BentNormalBufferAndTexelSizeValue);

		extern int32 GConeTraceDownsampleFactor;
		const float MinDownsampleFactor = GConeTraceDownsampleFactor;
		SetShaderValue(RHICmdList, ShaderRHI, MinDownsampleFactorToBaseLevel, MinDownsampleFactor);

		extern float GAOViewFadeDistanceScale;
		const float DistanceFadeScaleValue = 1.0f / ((1.0f - GAOViewFadeDistanceScale) * GetMaxAOViewDistance());
		SetShaderValue(RHICmdList, ShaderRHI, DistanceFadeScale, DistanceFadeScaleValue);


		SetShaderValue(RHICmdList, ShaderRHI, JitterOffset, JitterOffsetValue);
	}

	/** Serializer. */
	friend FArchive& operator<<(FArchive& Ar, FGeometryAwareUpsampleParameters& P)
	{
		Ar << P.DistanceFieldNormalTexture;
		Ar << P.DistanceFieldNormalSampler;
		Ar << P.BentNormalAOTexture;
		Ar << P.BentNormalAOSampler;
		Ar << P.DistanceFieldGBufferTexelSize;
		Ar << P.DistanceFieldGBufferJitterOffset;
		Ar << P.BentNormalBufferAndTexelSize;
		Ar << P.MinDownsampleFactorToBaseLevel;
		Ar << P.DistanceFadeScale;
		Ar << P.JitterOffset;
		return Ar;
	}

private:
	
		LAYOUT_FIELD(FShaderResourceParameter, DistanceFieldNormalTexture)
		LAYOUT_FIELD(FShaderResourceParameter, DistanceFieldNormalSampler)
		LAYOUT_FIELD(FShaderResourceParameter, BentNormalAOTexture)
		LAYOUT_FIELD(FShaderResourceParameter, BentNormalAOSampler)
		LAYOUT_FIELD(FShaderParameter, DistanceFieldGBufferTexelSize)
		LAYOUT_FIELD(FShaderParameter, DistanceFieldGBufferJitterOffset)
		LAYOUT_FIELD(FShaderParameter, BentNormalBufferAndTexelSize)
		LAYOUT_FIELD(FShaderParameter, MinDownsampleFactorToBaseLevel)
		LAYOUT_FIELD(FShaderParameter, DistanceFadeScale)
		LAYOUT_FIELD(FShaderParameter, JitterOffset)
	
};

class FUpdateHistoryDepthRejectionPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FUpdateHistoryDepthRejectionPS, Global);
public:

	// TODO(RDG) Hook these up to the shader. They are just informing RDG of transitions ATM.
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTextures)
		RDG_TEXTURE_ACCESS(DistanceFieldNormal, ERHIAccess::SRVGraphics)
		RDG_TEXTURE_ACCESS(DistanceFieldAOBentNormal, ERHIAccess::SRVGraphics)
		RDG_TEXTURE_ACCESS(BentNormalHistoryTexture, ERHIAccess::SRVGraphics)
		RDG_TEXTURE_ACCESS(VelocityTexture, ERHIAccess::SRVGraphics)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportDistanceFieldAO(Parameters.Platform) && IsUsingDistanceFields(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("DOWNSAMPLE_FACTOR"), GAODownsampleFactor);
	}

	/** Default constructor. */
	FUpdateHistoryDepthRejectionPS() {}

	/** Initialization constructor. */
	FUpdateHistoryDepthRejectionPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		AOParameters.Bind(Initializer.ParameterMap);
		GeometryAwareUpsampleParameters.Bind(Initializer.ParameterMap);
		BentNormalHistoryTexture.Bind(Initializer.ParameterMap, TEXT("BentNormalHistoryTexture"));
		BentNormalHistorySampler.Bind(Initializer.ParameterMap, TEXT("BentNormalHistorySampler"));
		HistoryWeight.Bind(Initializer.ParameterMap, TEXT("HistoryWeight"));
		HistoryDistanceThreshold.Bind(Initializer.ParameterMap, TEXT("HistoryDistanceThreshold"));
		UseHistoryFilter.Bind(Initializer.ParameterMap, TEXT("UseHistoryFilter"));
		VelocityTexture.Bind(Initializer.ParameterMap, TEXT("VelocityTexture"));
		VelocityTextureSampler.Bind(Initializer.ParameterMap, TEXT("VelocityTextureSampler"));
		HistoryScreenPositionScaleBias.Bind(Initializer.ParameterMap, TEXT("HistoryScreenPositionScaleBias"));
		HistoryUVMinMax.Bind(Initializer.ParameterMap, TEXT("HistoryUVMinMax"));
	}

	void SetParameters(
		FRHICommandList& RHICmdList, 
		const FViewInfo& View,
		const FIntRect& HistoryViewRect,
		FSceneRenderTargetItem& DistanceFieldNormal, 
		FSceneRenderTargetItem& DistanceFieldAOBentNormal,
		FSceneRenderTargetItem& BentNormalHistoryTextureValue, 
		IPooledRenderTarget* VelocityTextureValue,
		const FDistanceFieldAOParameters& Parameters)
	{
		FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();

		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);
		AOParameters.Set(RHICmdList, ShaderRHI, Parameters);
		GeometryAwareUpsampleParameters.Set(RHICmdList, ShaderRHI, View, DistanceFieldNormal, DistanceFieldAOBentNormal);

		SetTextureParameter(
			RHICmdList,
			ShaderRHI,
			BentNormalHistoryTexture,
			BentNormalHistorySampler,
			TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(),
			BentNormalHistoryTextureValue.ShaderResourceTexture
			);

		SetShaderValue(RHICmdList, ShaderRHI, HistoryWeight, GAOHistoryWeight);
		SetShaderValue(RHICmdList, ShaderRHI, HistoryDistanceThreshold, GAOHistoryDistanceThreshold);
		SetShaderValue(RHICmdList, ShaderRHI, UseHistoryFilter, UseAOHistoryStabilityPass() ? 1.0f : 0.0f);

		SetTextureParameter(
			RHICmdList,
			ShaderRHI,
			VelocityTexture,
			VelocityTextureSampler,
			TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(),
			VelocityTextureValue ? VelocityTextureValue->GetRenderTargetItem().ShaderResourceTexture : GBlackTexture->TextureRHI
			);

		{
			FIntPoint HistoryBufferSize = FSceneRenderTargets::Get(RHICmdList).GetBufferSizeXY() / FIntPoint(GAODownsampleFactor, GAODownsampleFactor);

			const float InvBufferSizeX = 1.0f / HistoryBufferSize.X;
			const float InvBufferSizeY = 1.0f / HistoryBufferSize.Y;

			const FVector4 HistoryScreenPositionScaleBiasValue(
				HistoryViewRect.Width() * InvBufferSizeX / +2.0f,
				HistoryViewRect.Height() * InvBufferSizeY / (-2.0f * GProjectionSignY),
				(HistoryViewRect.Height() / 2.0f + HistoryViewRect.Min.Y) * InvBufferSizeY,
				(HistoryViewRect.Width() / 2.0f + HistoryViewRect.Min.X) * InvBufferSizeX);

			// Pull in the max UV to exclude the region which will read outside the viewport due to bilinear filtering
			const FVector4 HistoryUVMinMaxValue(
				(HistoryViewRect.Min.X + 0.5f) * InvBufferSizeX,
				(HistoryViewRect.Min.Y + 0.5f) * InvBufferSizeY,
				(HistoryViewRect.Max.X - 0.5f) * InvBufferSizeX,
				(HistoryViewRect.Max.Y - 0.5f) * InvBufferSizeY);

			SetShaderValue(RHICmdList, ShaderRHI, HistoryScreenPositionScaleBias, HistoryScreenPositionScaleBiasValue);
			SetShaderValue(RHICmdList, ShaderRHI, HistoryUVMinMax, HistoryUVMinMaxValue);
		}
	}

private:
	LAYOUT_FIELD(FAOParameters, AOParameters);
	LAYOUT_FIELD(FGeometryAwareUpsampleParameters, GeometryAwareUpsampleParameters);
	LAYOUT_FIELD(FShaderResourceParameter, BentNormalHistoryTexture);
	LAYOUT_FIELD(FShaderResourceParameter, BentNormalHistorySampler);
	LAYOUT_FIELD(FShaderParameter, HistoryWeight);
	LAYOUT_FIELD(FShaderParameter, HistoryDistanceThreshold);
	LAYOUT_FIELD(FShaderParameter, UseHistoryFilter);
	LAYOUT_FIELD(FShaderResourceParameter, VelocityTexture);
	LAYOUT_FIELD(FShaderResourceParameter, VelocityTextureSampler);
	LAYOUT_FIELD(FShaderParameter, HistoryScreenPositionScaleBias);
	LAYOUT_FIELD(FShaderParameter, HistoryUVMinMax);
};

IMPLEMENT_SHADER_TYPE(, FUpdateHistoryDepthRejectionPS,TEXT("/Engine/Private/DistanceFieldLightingPost.usf"),TEXT("UpdateHistoryDepthRejectionPS"),SF_Pixel);


template<bool bManuallyClampUV>
class TFilterHistoryPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(TFilterHistoryPS, Global);
public:
	
	// TODO(RDG) Hook these up to the shader. They are just informing RDG of transitions ATM.
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTextures)
		RDG_TEXTURE_ACCESS(DistanceFieldNormal, ERHIAccess::SRVGraphics)
		RDG_TEXTURE_ACCESS(NewBentNormalHistory, ERHIAccess::SRVGraphics)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportDistanceFieldAO(Parameters.Platform) && IsUsingDistanceFields(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("DOWNSAMPLE_FACTOR"), GAODownsampleFactor);
		OutEnvironment.SetDefine(TEXT("MANUALLY_CLAMP_UV"), bManuallyClampUV);
	}

	/** Default constructor. */
	TFilterHistoryPS() {}

	/** Initialization constructor. */
	TFilterHistoryPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		BentNormalAOTexture.Bind(Initializer.ParameterMap, TEXT("BentNormalAOTexture"));
		BentNormalAOSampler.Bind(Initializer.ParameterMap, TEXT("BentNormalAOSampler"));
		HistoryWeight.Bind(Initializer.ParameterMap, TEXT("HistoryWeight"));
		BentNormalAOTexelSize.Bind(Initializer.ParameterMap, TEXT("BentNormalAOTexelSize"));
		MaxSampleBufferUV.Bind(Initializer.ParameterMap, TEXT("MaxSampleBufferUV"));
		DistanceFieldNormalTexture.Bind(Initializer.ParameterMap, TEXT("DistanceFieldNormalTexture"));
		DistanceFieldNormalSampler.Bind(Initializer.ParameterMap, TEXT("DistanceFieldNormalSampler"));
	}

	void SetParameters(
		FRHICommandList& RHICmdList, 
		const FViewInfo& View, 
		FSceneRenderTargetItem& DistanceFieldNormal, 
		FSceneRenderTargetItem& BentNormalHistoryTextureValue)
	{
		FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();

		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);

		SetTextureParameter(
			RHICmdList,
			ShaderRHI,
			BentNormalAOTexture,
			BentNormalAOSampler,
			TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(),
			BentNormalHistoryTextureValue.ShaderResourceTexture
			);

		SetTextureParameter(
			RHICmdList,
			ShaderRHI,
			DistanceFieldNormalTexture,
			DistanceFieldNormalSampler,
			TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(),
			DistanceFieldNormal.ShaderResourceTexture
			);

		SetShaderValue(RHICmdList, ShaderRHI, HistoryWeight, GAOHistoryWeight);
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
		
		const FIntPoint DownsampledBufferSize(SceneContext.GetBufferSizeXY() / FIntPoint(GAODownsampleFactor, GAODownsampleFactor));
		const FVector2D BaseLevelTexelSizeValue(1.0f / DownsampledBufferSize.X, 1.0f / DownsampledBufferSize.Y);
		SetShaderValue(RHICmdList, ShaderRHI, BentNormalAOTexelSize, BaseLevelTexelSizeValue);

		if (bManuallyClampUV)
		{
			FVector2D MaxSampleBufferUVValue(
				(View.ViewRect.Width() / GAODownsampleFactor - 0.5f - GAODownsampleFactor) / DownsampledBufferSize.X,
				(View.ViewRect.Height() / GAODownsampleFactor - 0.5f - GAODownsampleFactor) / DownsampledBufferSize.Y);
			SetShaderValue(RHICmdList, ShaderRHI, MaxSampleBufferUV, MaxSampleBufferUVValue);
		}
	}

private:

	LAYOUT_FIELD(FShaderResourceParameter, BentNormalAOTexture);
	LAYOUT_FIELD(FShaderResourceParameter, BentNormalAOSampler);
	LAYOUT_FIELD(FShaderParameter, HistoryWeight);
	LAYOUT_FIELD(FShaderParameter, BentNormalAOTexelSize);
	LAYOUT_FIELD(FShaderParameter, MaxSampleBufferUV);
	LAYOUT_FIELD(FShaderResourceParameter, DistanceFieldNormalTexture);
	LAYOUT_FIELD(FShaderResourceParameter, DistanceFieldNormalSampler);
};


#define VARIATION1(A) \
	typedef TFilterHistoryPS<A> TFilterHistoryPS##A; \
	IMPLEMENT_SHADER_TYPE(template<>,TFilterHistoryPS##A,TEXT("/Engine/Private/DistanceFieldLightingPost.usf"),TEXT("FilterHistoryPS"),SF_Pixel);

VARIATION1(false)
VARIATION1(true)

#undef VARIATION1

class FGeometryAwareUpsamplePS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FGeometryAwareUpsamplePS, Global);
public:

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		RDG_TEXTURE_ACCESS(DistanceFieldNormal, ERHIAccess::SRVGraphics)
		RDG_TEXTURE_ACCESS(BentNormalInterpolation, ERHIAccess::SRVGraphics)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportDistanceFieldAO(Parameters.Platform) && IsUsingDistanceFields(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("DOWNSAMPLE_FACTOR"), GAODownsampleFactor);
	}

	/** Default constructor. */
	FGeometryAwareUpsamplePS() {}

	/** Initialization constructor. */
	FGeometryAwareUpsamplePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		AOParameters.Bind(Initializer.ParameterMap);
		GeometryAwareUpsampleParameters.Bind(Initializer.ParameterMap);
	}

	void SetParameters(
		FRHICommandList& RHICmdList,
		const FViewInfo& View,
		FSceneRenderTargetItem& DistanceFieldNormal,
		FSceneRenderTargetItem& DistanceFieldAOBentNormal,
		const FDistanceFieldAOParameters& Parameters)
	{
		FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();

		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);

		AOParameters.Set(RHICmdList, ShaderRHI, Parameters);
		GeometryAwareUpsampleParameters.Set(RHICmdList, ShaderRHI, View, DistanceFieldNormal, DistanceFieldAOBentNormal);
	}

private:
	LAYOUT_FIELD(FAOParameters, AOParameters);
	LAYOUT_FIELD(FGeometryAwareUpsampleParameters, GeometryAwareUpsampleParameters);
};

IMPLEMENT_SHADER_TYPE(,FGeometryAwareUpsamplePS, TEXT("/Engine/Private/DistanceFieldLightingPost.usf"), TEXT("GeometryAwareUpsamplePS"), SF_Pixel);

void AllocateOrReuseAORenderTarget(FRDGBuilder& GraphBuilder, FRDGTextureRef& Texture, const TCHAR* Name, EPixelFormat Format, ETextureCreateFlags Flags) 
{
	if (!Texture)
	{
		const FIntPoint BufferSize = GetBufferSizeForAO();
		const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(BufferSize, Format, FClearValueBinding::None, Flags | TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV);
		Texture = GraphBuilder.CreateTexture(Desc, Name);
		AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(Texture), FLinearColor::Black);
	}
}

void GeometryAwareUpsample(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef DistanceFieldAOBentNormal, FRDGTextureRef DistanceFieldNormal, FRDGTextureRef BentNormalInterpolation, const FDistanceFieldAOParameters& Parameters)
{
	auto* PassParameters = GraphBuilder.AllocParameters<FGeometryAwareUpsamplePS::FParameters>();
	PassParameters->DistanceFieldNormal = DistanceFieldNormal;
	PassParameters->BentNormalInterpolation = BentNormalInterpolation;
	PassParameters->RenderTargets[0] = FRenderTargetBinding(DistanceFieldAOBentNormal, ERenderTargetLoadAction::ELoad);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("GeometryAwareUpsample"),
		PassParameters,
		ERDGPassFlags::Raster,
		[&View, DistanceFieldNormal, BentNormalInterpolation, Parameters](FRHICommandList& RHICmdList)
	{
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

		RHICmdList.SetViewport(0, 0, 0.0f, View.ViewRect.Width() / GAODownsampleFactor, View.ViewRect.Height() / GAODownsampleFactor, 1.0f);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();

		TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		TShaderMapRef<FGeometryAwareUpsamplePS> PixelShader(View.ShaderMap);
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
		PixelShader->SetParameters(RHICmdList, View, DistanceFieldNormal->GetPooledRenderTarget()->GetRenderTargetItem(), BentNormalInterpolation->GetPooledRenderTarget()->GetRenderTargetItem(), Parameters);
		VertexShader->SetParameters(RHICmdList, View.ViewUniformBuffer);

		DrawRectangle(
			RHICmdList,
			0, 0,
			View.ViewRect.Width() / GAODownsampleFactor, View.ViewRect.Height() / GAODownsampleFactor,
			0, 0,
			View.ViewRect.Width() / GAODownsampleFactor, View.ViewRect.Height() / GAODownsampleFactor,
			FIntPoint(View.ViewRect.Width() / GAODownsampleFactor, View.ViewRect.Height() / GAODownsampleFactor),
			SceneContext.GetBufferSizeXY() / FIntPoint(GAODownsampleFactor, GAODownsampleFactor),
			VertexShader);
	});
}

void UpdateHistory(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View, 
	const TCHAR* BentNormalHistoryRTName,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
	FRDGTextureRef VelocityTexture,
	FRDGTextureRef DistanceFieldNormal,
	FRDGTextureRef BentNormalInterpolation,
	/** Contains last frame's history, if non-NULL.  This will be updated with the new frame's history. */
	FIntRect* DistanceFieldAOHistoryViewRect,
	TRefCountPtr<IPooledRenderTarget>* BentNormalHistoryState,
	/** Output of Temporal Reprojection for the next step in the pipeline. */
	FRDGTextureRef& BentNormalHistoryOutput,
	const FDistanceFieldAOParameters& Parameters)
{
	const FIntPoint SceneTextureExtent = FSceneRenderTargets::Get(GraphBuilder.RHICmdList).GetBufferSizeXY();

	if (BentNormalHistoryState && GAOUseHistory)
	{
#if WITH_MGPU
		AddPass(GraphBuilder, [&View](FRHICommandList& RHICmdList)
		{
			static const FName NameForTemporalEffect("DistanceFieldAOHistory");
			SCOPED_GPU_STAT(RHICmdList, AFRWaitForDistanceFieldAOHistory);
			RHICmdList.WaitForTemporalEffect(FName(NameForTemporalEffect, View.ViewState->UniqueID));
		});
#endif

		FIntPoint BufferSize = GetBufferSizeForAO();

		if (*BentNormalHistoryState 
			&& !View.bCameraCut 
			&& !View.bPrevTransformsReset
			&& !GAOClearHistory
			// If the scene render targets reallocate, toss the history so we don't read uninitialized data
			&& (*BentNormalHistoryState)->GetDesc().Extent == BufferSize)
		{
			FRDGTextureRef BentNormalHistoryTexture = GraphBuilder.RegisterExternalTexture(*BentNormalHistoryState);

			ETextureCreateFlags HistoryPassOutputFlags = ETextureCreateFlags(UseAOHistoryStabilityPass() ? GFastVRamConfig.DistanceFieldAOHistory : TexCreate_None);
			// Reuse a render target from the pool with a consistent name, for vis purposes
			FRDGTextureRef NewBentNormalHistory = nullptr;
			AllocateOrReuseAORenderTarget(GraphBuilder, NewBentNormalHistory, BentNormalHistoryRTName, PF_FloatRGBA, HistoryPassOutputFlags);

			{
				FIntRect PrevHistoryViewRect = *DistanceFieldAOHistoryViewRect;

				auto* PassParameters = GraphBuilder.AllocParameters<FUpdateHistoryDepthRejectionPS::FParameters>();
				PassParameters->SceneTextures = SceneTexturesUniformBuffer;
				PassParameters->DistanceFieldNormal = DistanceFieldNormal;
				PassParameters->DistanceFieldAOBentNormal = BentNormalInterpolation;
				PassParameters->BentNormalHistoryTexture = BentNormalHistoryTexture;
				PassParameters->VelocityTexture = VelocityTexture;
				PassParameters->RenderTargets[0] = FRenderTargetBinding(NewBentNormalHistory, ERenderTargetLoadAction::ELoad);

				TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
				TShaderMapRef<FUpdateHistoryDepthRejectionPS> PixelShader(View.ShaderMap);

				GraphBuilder.AddPass(
					RDG_EVENT_NAME("UpdateHistory"),
					PassParameters,
					ERDGPassFlags::Raster,
					[VertexShader, PixelShader, &View, DistanceFieldNormal, BentNormalInterpolation, BentNormalHistoryTexture, VelocityTexture, PrevHistoryViewRect, Parameters, SceneTextureExtent]
					(FRHICommandList& RHICmdList)
				{
					RHICmdList.SetViewport(0, 0, 0.0f, View.ViewRect.Width() / GAODownsampleFactor, View.ViewRect.Height() / GAODownsampleFactor, 1.0f);

					FGraphicsPipelineStateInitializer GraphicsPSOInit;
					RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
					GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
					GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
					GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
					GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
					GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
					GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
					GraphicsPSOInit.PrimitiveType = PT_TriangleList;

					SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

					PixelShader->SetParameters(RHICmdList, View, PrevHistoryViewRect,
						DistanceFieldNormal->GetPooledRenderTarget()->GetRenderTargetItem(),
						BentNormalInterpolation->GetPooledRenderTarget()->GetRenderTargetItem(),
						BentNormalHistoryTexture->GetPooledRenderTarget()->GetRenderTargetItem(),
						VelocityTexture ? VelocityTexture->GetPooledRenderTarget() : nullptr,
						Parameters);

					VertexShader->SetParameters(RHICmdList, View.ViewUniformBuffer);

					DrawRectangle(
						RHICmdList,
						0, 0,
						View.ViewRect.Width() / GAODownsampleFactor, View.ViewRect.Height() / GAODownsampleFactor,
						View.ViewRect.Min.X / GAODownsampleFactor, View.ViewRect.Min.Y / GAODownsampleFactor,
						View.ViewRect.Width() / GAODownsampleFactor, View.ViewRect.Height() / GAODownsampleFactor,
						FIntPoint(View.ViewRect.Width() / GAODownsampleFactor, View.ViewRect.Height() / GAODownsampleFactor),
						SceneTextureExtent / FIntPoint(GAODownsampleFactor, GAODownsampleFactor),
						VertexShader);
				});
			}

			if (UseAOHistoryStabilityPass())
			{
				const FRDGTextureDesc& HistoryDesc = BentNormalHistoryTexture->Desc;

				// Reallocate history if buffer sizes have changed
				if (HistoryDesc.Extent != SceneTextureExtent / FIntPoint(GAODownsampleFactor, GAODownsampleFactor))
				{
					GRenderTargetPool.FreeUnusedResource(*BentNormalHistoryState);
					*BentNormalHistoryState = nullptr;
					// Update the view state's render target reference with the new history
					AllocateOrReuseAORenderTarget(GraphBuilder, BentNormalHistoryTexture, BentNormalHistoryRTName, PF_FloatRGBA);
				}

				auto* PassParameters = GraphBuilder.AllocParameters<FUpdateHistoryDepthRejectionPS::FParameters>();
				PassParameters->SceneTextures = SceneTexturesUniformBuffer;
				PassParameters->DistanceFieldNormal = DistanceFieldNormal;
				PassParameters->BentNormalHistoryTexture = NewBentNormalHistory;
				PassParameters->RenderTargets[0] = FRenderTargetBinding(BentNormalHistoryTexture, ERenderTargetLoadAction::ELoad);

				GraphBuilder.AddPass(
					RDG_EVENT_NAME("UpdateHistoryStability"),
					PassParameters,
					ERDGPassFlags::Raster,
					[&View, DistanceFieldNormal, NewBentNormalHistory, SceneTextureExtent](FRHICommandList& RHICmdList)
				{
					RHICmdList.SetViewport(0, 0, 0.0f, View.ViewRect.Width() / GAODownsampleFactor, View.ViewRect.Height() / GAODownsampleFactor, 1.0f);

					TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);

					FGraphicsPipelineStateInitializer GraphicsPSOInit;
					RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
					GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
					GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
					GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
					GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
					GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
					GraphicsPSOInit.PrimitiveType = PT_TriangleList;

					if (View.ViewRect.Min == FIntPoint::ZeroValue && View.ViewRect.Max == SceneTextureExtent)
					{
						TShaderMapRef<TFilterHistoryPS<false> > PixelShader(View.ShaderMap);

						GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
						SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
						PixelShader->SetParameters(RHICmdList, View, DistanceFieldNormal->GetPooledRenderTarget()->GetRenderTargetItem(), NewBentNormalHistory->GetPooledRenderTarget()->GetRenderTargetItem());
					}
					else
					{
						TShaderMapRef<TFilterHistoryPS<true> > PixelShader(View.ShaderMap);

						GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
						SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
						PixelShader->SetParameters(RHICmdList, View, DistanceFieldNormal->GetPooledRenderTarget()->GetRenderTargetItem(), NewBentNormalHistory->GetPooledRenderTarget()->GetRenderTargetItem());
					}

					VertexShader->SetParameters(RHICmdList, View.ViewUniformBuffer);

					DrawRectangle(
						RHICmdList,
						0, 0,
						View.ViewRect.Width() / GAODownsampleFactor, View.ViewRect.Height() / GAODownsampleFactor,
						0, 0,
						View.ViewRect.Width() / GAODownsampleFactor, View.ViewRect.Height() / GAODownsampleFactor,
						FIntPoint(View.ViewRect.Width() / GAODownsampleFactor, View.ViewRect.Height() / GAODownsampleFactor),
						SceneTextureExtent / FIntPoint(GAODownsampleFactor, GAODownsampleFactor),
						VertexShader);
				});

				GraphBuilder.QueueTextureExtraction(BentNormalHistoryTexture, BentNormalHistoryState);
				BentNormalHistoryOutput = BentNormalHistoryTexture;
			}
			else
			{
				// Update the view state's render target reference with the new history
				GraphBuilder.QueueTextureExtraction(NewBentNormalHistory, BentNormalHistoryState);
				BentNormalHistoryOutput = NewBentNormalHistory;
			}
		}
		else
		{
			// Use the current frame's upscaled mask for next frame's history
			FRDGTextureRef DistanceFieldAOBentNormal = nullptr;
			AllocateOrReuseAORenderTarget(GraphBuilder, DistanceFieldAOBentNormal, TEXT("DistanceFieldBentNormalAO"), PF_FloatRGBA, GFastVRamConfig.DistanceFieldAOBentNormal);

			GeometryAwareUpsample(GraphBuilder, View, DistanceFieldAOBentNormal, DistanceFieldNormal, BentNormalInterpolation, Parameters);

			GraphBuilder.QueueTextureExtraction(DistanceFieldAOBentNormal, BentNormalHistoryState);
			BentNormalHistoryOutput = DistanceFieldAOBentNormal;
		}

		DistanceFieldAOHistoryViewRect->Min = FIntPoint::ZeroValue;
		DistanceFieldAOHistoryViewRect->Max.X = View.ViewRect.Size().X / GAODownsampleFactor;
		DistanceFieldAOHistoryViewRect->Max.Y = View.ViewRect.Size().Y / GAODownsampleFactor;

#if WITH_MGPU && 0 // TODO(RDG)
		FRHITexture* TexturesToCopyForTemporalEffect[] = { BentNormalHistoryOutput->GetRenderTargetItem().ShaderResourceTexture.GetReference() };
		RHICmdList.BroadcastTemporalEffect(FName(NameForTemporalEffect, View.ViewState->UniqueID), TexturesToCopyForTemporalEffect);
#endif
	}
	else
	{
		// Temporal reprojection is disabled or there is no view state - just upscale
		FRDGTextureRef DistanceFieldAOBentNormal = nullptr;
		AllocateOrReuseAORenderTarget(GraphBuilder, DistanceFieldAOBentNormal, TEXT("DistanceFieldBentNormalAO"), PF_FloatRGBA, GFastVRamConfig.DistanceFieldAOBentNormal);

		GeometryAwareUpsample(GraphBuilder, View, DistanceFieldAOBentNormal, DistanceFieldNormal, BentNormalInterpolation, Parameters);

		BentNormalHistoryOutput = DistanceFieldAOBentNormal;
	}
}

class FDistanceFieldAOUpsamplePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDistanceFieldAOUpsamplePS);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTextures)
		RDG_TEXTURE_ACCESS(DistanceFieldAOBentNormal, ERHIAccess::SRVGraphics)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	class FModulateToSceneColorDim : SHADER_PERMUTATION_BOOL("MODULATE_SCENE_COLOR");
	using FPermutationDomain = TShaderPermutationDomain<FModulateToSceneColorDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && DoesPlatformSupportDistanceFieldAO(Parameters.Platform) && IsUsingDistanceFields(Parameters.Platform);
	}

	FDistanceFieldAOUpsamplePS() = default;
	FDistanceFieldAOUpsamplePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		BindForLegacyShaderParameters<FParameters>(this, Initializer.PermutationId, Initializer.ParameterMap, false);
		DFAOUpsampleParameters.Bind(Initializer.ParameterMap);
		MinIndirectDiffuseOcclusion.Bind(Initializer.ParameterMap,TEXT("MinIndirectDiffuseOcclusion"));
	}

	void SetParameters(FRHICommandList& RHICmdList, const FViewInfo& View, const TRefCountPtr<IPooledRenderTarget>& DistanceFieldAOBentNormal)
	{
		FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();

		DFAOUpsampleParameters.Set(RHICmdList, ShaderRHI, View, DistanceFieldAOBentNormal);

		FScene* Scene = (FScene*)View.Family->Scene;
		const float MinOcclusion = Scene->SkyLight ? Scene->SkyLight->MinOcclusion : 0;
		SetShaderValue(RHICmdList, ShaderRHI, MinIndirectDiffuseOcclusion, MinOcclusion);
	}
	
private:
	LAYOUT_FIELD(FDFAOUpsampleParameters, DFAOUpsampleParameters);
	LAYOUT_FIELD(FShaderParameter, MinIndirectDiffuseOcclusion);
};

IMPLEMENT_GLOBAL_SHADER(FDistanceFieldAOUpsamplePS, "/Engine/Private/DistanceFieldLightingPost.usf", "AOUpsamplePS", SF_Pixel);

void UpsampleBentNormalAO(
	FRDGBuilder& GraphBuilder,
	const TArray<FViewInfo>& Views,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
	FRDGTextureRef SceneColorTexture,
	FRDGTextureRef DistanceFieldAOBentNormal,
	bool bModulateSceneColor)
{
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];
		RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);

		auto* PassParameters = GraphBuilder.AllocParameters<FDistanceFieldAOUpsamplePS::FParameters>();
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->SceneTextures = SceneTexturesUniformBuffer;
		PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneColorTexture, ERenderTargetLoadAction::ELoad);
		PassParameters->DistanceFieldAOBentNormal = DistanceFieldAOBentNormal;

		TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);

		FDistanceFieldAOUpsamplePS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FDistanceFieldAOUpsamplePS::FModulateToSceneColorDim>(bModulateSceneColor);
		TShaderMapRef<FDistanceFieldAOUpsamplePS> PixelShader(View.ShaderMap, PermutationVector);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("UpsampleAO"),
			PassParameters,
			ERDGPassFlags::Raster,
			[VertexShader, PixelShader, PassParameters, &View, DistanceFieldAOBentNormal, bModulateSceneColor](FRHICommandList& RHICmdList)
		{
			RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			if (bModulateSceneColor)
			{
				GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_DestColor, BF_Zero, BO_Add, BF_Zero, BF_One>::GetRHI();
			}
			else
			{
				GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
			}

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
			PixelShader->SetParameters(RHICmdList, View, DistanceFieldAOBentNormal->GetPooledRenderTarget());
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PassParameters);

			DrawRectangle(
				RHICmdList,
				0, 0,
				View.ViewRect.Width(), View.ViewRect.Height(),
				View.ViewRect.Min.X / GAODownsampleFactor, View.ViewRect.Min.Y / GAODownsampleFactor,
				View.ViewRect.Width() / GAODownsampleFactor, View.ViewRect.Height() / GAODownsampleFactor,
				FIntPoint(View.ViewRect.Width(), View.ViewRect.Height()),
				GetBufferSizeForAO(),
				VertexShader);
		});
	}
}
