// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessHistogram.cpp: Post processing histogram implementation.
=============================================================================*/

#include "PostProcess/PostProcessHistogram.h"
#include "SceneUtils.h"
#include "PostProcess/PostProcessEyeAdaptation.h"


class FPostProcessClearHistogramCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessClearHistogramCS, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), FRCPassPostProcessHistogram::ThreadGroupSizeX);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), FRCPassPostProcessHistogram::ThreadGroupSizeY);
		OutEnvironment.SetDefine(TEXT("LOOP_SIZEX"), FRCPassPostProcessHistogram::LoopCountX);
		OutEnvironment.SetDefine(TEXT("LOOP_SIZEY"), FRCPassPostProcessHistogram::LoopCountY);
		OutEnvironment.SetDefine(TEXT("HISTOGRAM_SIZE"), FRCPassPostProcessHistogram::HistogramSize);
		OutEnvironment.SetDefine(TEXT("EYE_ADAPTATION_PARAMS_SIZE"), (uint32)EYE_ADAPTATION_PARAMS_SIZE);
		OutEnvironment.CompilerFlags.Add(CFLAG_StandardOptimization);
	}

	/** Default constructor. */
	FPostProcessClearHistogramCS() {}

public:
	FRWShaderParameter HistogramRWTexture;


	/** Initialization constructor. */
	FPostProcessClearHistogramCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		HistogramRWTexture.Bind(Initializer.ParameterMap, TEXT("HistogramTexture"));
	}

	// FShader interface.
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << HistogramRWTexture;
		return bShaderHasOutdatedParameters;
	}
};
IMPLEMENT_SHADER_TYPE(, FPostProcessClearHistogramCS, TEXT("/Engine/Private/PostProcessHistogram.usf"), TEXT("ClearHistogramCS"), SF_Compute);


/** Encapsulates the post processing histogram compute shader. */
class FPostProcessHistogramCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPostProcessHistogramCS, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), FRCPassPostProcessHistogram::ThreadGroupSizeX);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), FRCPassPostProcessHistogram::ThreadGroupSizeY);
		OutEnvironment.SetDefine(TEXT("LOOP_SIZEX"), FRCPassPostProcessHistogram::LoopCountX);
		OutEnvironment.SetDefine(TEXT("LOOP_SIZEY"), FRCPassPostProcessHistogram::LoopCountY);
		OutEnvironment.SetDefine(TEXT("HISTOGRAM_SIZE"), FRCPassPostProcessHistogram::HistogramSize);
		OutEnvironment.SetDefine(TEXT("EYE_ADAPTATION_PARAMS_SIZE"), (uint32)EYE_ADAPTATION_PARAMS_SIZE);
		OutEnvironment.CompilerFlags.Add( CFLAG_StandardOptimization );
	}

	/** Default constructor. */
	FPostProcessHistogramCS() {}

public:
	FPostProcessPassParameters PostprocessParameter;
	FRWShaderParameter HistogramRWTexture;
	FShaderParameter HistogramParameters;
	FShaderParameter ThreadGroupCount;
	FShaderParameter LeftTopOffset;
	FShaderParameter EyeAdaptationParams;

	/** Initialization constructor. */
	FPostProcessHistogramCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		PostprocessParameter.Bind(Initializer.ParameterMap);
		HistogramRWTexture.Bind(Initializer.ParameterMap, TEXT("HistogramTexture"));
		HistogramParameters.Bind(Initializer.ParameterMap, TEXT("HistogramParameters"));
		ThreadGroupCount.Bind(Initializer.ParameterMap, TEXT("ThreadGroupCount"));
		LeftTopOffset.Bind(Initializer.ParameterMap, TEXT("LeftTopOffset"));
		EyeAdaptationParams.Bind(Initializer.ParameterMap, TEXT("EyeAdaptationParams"));
	}

	void SetCS(FRHICommandList& RHICmdList, const FRenderingCompositePassContext& Context, FIntPoint ThreadGroupCountValue, FIntPoint LeftTopOffsetValue, FIntPoint GatherExtent)
	{
		const FComputeShaderRHIParamRef ShaderRHI = GetComputeShader();

		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, Context.View.ViewUniformBuffer);

		PostprocessParameter.SetCS(ShaderRHI, Context, Context.RHICmdList, TStaticSamplerState<SF_Point,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI());

		SetShaderValue(RHICmdList, ShaderRHI, ThreadGroupCount, ThreadGroupCountValue);
		SetShaderValue(RHICmdList, ShaderRHI, LeftTopOffset, LeftTopOffsetValue);

		FVector4 HistogramParametersValue(GatherExtent.X, GatherExtent.Y, 0, 0);
		SetShaderValue(RHICmdList, ShaderRHI, HistogramParameters, HistogramParametersValue);

		{
			FVector4 Temp[EYE_ADAPTATION_PARAMS_SIZE];

			FRCPassPostProcessEyeAdaptation::ComputeEyeAdaptationParamsValue(Context.View, Temp);
			SetShaderValueArray(RHICmdList, ShaderRHI, EyeAdaptationParams, Temp, EYE_ADAPTATION_PARAMS_SIZE);
		}
	}
	
	// FShader interface.
	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << PostprocessParameter << HistogramRWTexture << HistogramParameters << ThreadGroupCount << LeftTopOffset << EyeAdaptationParams;
		return bShaderHasOutdatedParameters;
	}
};

IMPLEMENT_SHADER_TYPE(,FPostProcessHistogramCS,TEXT("/Engine/Private/PostProcessHistogram.usf"),TEXT("MainCS"),SF_Compute);

void FRCPassPostProcessHistogram::Process(FRenderingCompositePassContext& Context)
{
	SCOPED_DRAW_EVENT(Context.RHICmdList, PostProcessHistogram);
	const FPooledRenderTargetDesc* InputDesc = GetInputDesc(ePId_Input0);

	if(!InputDesc)
	{
		// input is not hooked up correctly
		return;
	}

	const FViewInfo& View = Context.View;
	const FSceneViewFamily& ViewFamily = *(View.Family);
	
	FIntPoint SrcSize = InputDesc->Extent;
	FIntRect DestRect = Context.SceneColorViewRect;

	const FSceneRenderTargetItem& DestRenderTarget = PassOutputs[0].RequestSurface(Context);
	
	UnbindRenderTargets(Context.RHICmdList);
	// Clear histogram counters
	check(DestRenderTarget.UAV);
	Context.RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToCompute, DestRenderTarget.UAV);
	SetRenderTarget(Context.RHICmdList, FTextureRHIRef(), FTextureRHIRef());
	{
		TShaderMapRef<FPostProcessClearHistogramCS> ComputeShader(Context.GetShaderMap());
		const FComputeShaderRHIParamRef ShaderRHI = ComputeShader->GetComputeShader();
		Context.RHICmdList.SetComputeShader(ShaderRHI);
		ComputeShader->HistogramRWTexture.SetTexture(Context.RHICmdList, ShaderRHI, nullptr, DestRenderTarget.UAV);
		DispatchComputeShader(Context.RHICmdList, *ComputeShader, 1, 1, 1);
	}

	TShaderMapRef<FPostProcessHistogramCS> ComputeShader(Context.GetShaderMap());
	const FComputeShaderRHIParamRef ShaderRHI = ComputeShader->GetComputeShader();
	Context.RHICmdList.SetComputeShader(ShaderRHI);
 	ComputeShader->HistogramRWTexture.SetTexture(Context.RHICmdList, ShaderRHI, nullptr, DestRenderTarget.UAV);

	FIntPoint GatherExtent = ComputeGatherExtent(Context);
	FIntPoint ThreadGroupCountValue = ComputeThreadGroupCount(GatherExtent);

	ComputeShader->SetCS(Context.RHICmdList, Context, ThreadGroupCountValue, (DestRect.Min + FIntPoint(1, 1)) / 2, GatherExtent);
	DispatchComputeShader(Context.RHICmdList, *ComputeShader, ThreadGroupCountValue.X, ThreadGroupCountValue.Y, 1);

	// unset destination
	ComputeShader->HistogramRWTexture.UnsetUAV(Context.RHICmdList, ComputeShader->GetComputeShader());

	Context.RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, DestRenderTarget.UAV);
	ensureMsgf(DestRenderTarget.TargetableTexture == DestRenderTarget.ShaderResourceTexture, TEXT("%s should be resolved to a separate SRV"), *DestRenderTarget.TargetableTexture->GetName().ToString());	
}


FIntPoint FRCPassPostProcessHistogram::ComputeGatherExtent(const FRenderingCompositePassContext& Context)
{
	// we currently assume the input is half res, one full res pixel less to avoid getting bilinear filtered input
	return (Context.SceneColorViewRect.Size() - FIntPoint(1, 1)) / 2;
}

FIntPoint FRCPassPostProcessHistogram::ComputeThreadGroupCount(FIntPoint PixelExtent)
{
	uint32 TexelPerThreadGroupX = ThreadGroupSizeX * LoopCountX;
	uint32 TexelPerThreadGroupY = ThreadGroupSizeY * LoopCountY;

	uint32 ThreadGroupCountX = (PixelExtent.X + TexelPerThreadGroupX - 1) / TexelPerThreadGroupX;
	uint32 ThreadGroupCountY = (PixelExtent.Y + TexelPerThreadGroupY - 1) / TexelPerThreadGroupY;

	return FIntPoint(ThreadGroupCountX, ThreadGroupCountY);
}

FPooledRenderTargetDesc FRCPassPostProcessHistogram::ComputeOutputDesc(EPassOutputId InPassOutputId) const
{
	FPooledRenderTargetDesc UnmodifiedRet = GetInput(ePId_Input0)->GetOutput()->RenderTargetDesc;

	UnmodifiedRet.Reset();
	FIntPoint PixelExtent = UnmodifiedRet.Extent;

	FIntPoint ThreadGroupCount = ComputeThreadGroupCount(PixelExtent);

	// One shared histogram (groups counts accumulated using integer atomics)
	FIntPoint NewSize = FIntPoint(HistogramSize, 1);
	FPooledRenderTargetDesc Ret(FPooledRenderTargetDesc::Create2DDesc(NewSize, PF_R32_UINT, FClearValueBinding::None, TexCreate_None, TexCreate_UAV, false));

	Ret.Flags |= GFastVRamConfig.Histogram;
	Ret.DebugName = TEXT("Histogram");

	return Ret;
}
