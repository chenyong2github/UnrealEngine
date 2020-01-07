// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessAmbientOcclusion.h: Post processing ambient occlusion implementation.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "UniformBuffer.h"
#include "RendererInterface.h"
#include "PostProcess/RenderingCompositionGraph.h"

class FViewInfo;

enum class ESSAOType
{
	// pixel shader
	EPS,
	// non async compute shader
	ECS,
	// async compute shader
	EAsyncCS,
};



enum class EGTAOType
{
	// Not on (use legacy if at all)
	EOff,

	// Async compute shader where the Horizon Search and Inner Integrate are combined and the spatial filter is run on the Async Pipe
	// Temporal and Upsample are run on the GFX Pipe as Temporal requires velocity Buffer
	EAsyncCombinedSpatial,

	// Async compute shader where the Horizon Search is run on the Async compute pipe
	// Integrate, Spatial, Temporal and Upsample are run on the GFX pipe as these require GBuffer channels
	EAsyncHorizonSearch,

	// Non async version where all passes are run on the GFX Pipe
	ENonAsync,
};

class FSSAOHelper
{
public:

	// Utility functions for deciding AO logic.
	// for render thread
	// @return usually in 0..100 range but could be outside, combines the view with the cvar setting
	static float GetAmbientOcclusionQualityRT(const FSceneView& View);

	// @return returns actual shader quality level to use. 0-4 currently.
	static int32 GetAmbientOcclusionShaderLevel(const FSceneView& View);

	// @return whether AmbientOcclusion should run a compute shader.
	static bool IsAmbientOcclusionCompute(const FSceneView& View);

	static int32 GetNumAmbientOcclusionLevels();
	static float GetAmbientOcclusionStepMipLevelFactor();
	static EAsyncComputeBudget GetAmbientOcclusionAsyncComputeBudget();

	static bool IsBasePassAmbientOcclusionRequired(const FViewInfo& View);
	static bool IsAmbientOcclusionAsyncCompute(const FViewInfo& View, uint32 AOPassCount);

	// @return 0:off, 0..3
	static uint32 ComputeAmbientOcclusionPassCount(const FViewInfo& View);

	static EGTAOType GetGTAOPassType(const FViewInfo& View);
};

// ePId_Input0: SceneDepth
// ePId_Input1: optional from former downsampling pass
// derives from TRenderingCompositePassBase<InputCount, OutputCount> 
class FRCPassPostProcessAmbientOcclusionSetup : public TRenderingCompositePassBase<2, 1>
{
public:

	// interface FRenderingCompositePass ---------
	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual void Release() override { delete this; }
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;

private:
	// otherwise this is a down sampling pass which takes two MRT inputs from the setup pass before
	bool IsInitialPass() const;
};

// ePId_Input0: Lower resolution AO result buffer
class FRCPassPostProcessAmbientOcclusionSmooth : public TRenderingCompositePassBase<1, 1>
{
public:
	static constexpr int32 ThreadGroupSize1D = 8;

	FRCPassPostProcessAmbientOcclusionSmooth(ESSAOType InAOType, bool bInDirectOutput = false);

	// interface FRenderingCompositePass ---------
	virtual void Process(FRenderingCompositePassContext& Context) final override;
	virtual void Release() final override { delete this; }
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const final override;

private:
	template <typename TRHICmdList>
	void DispatchCS(
		TRHICmdList& RHICmdList,
		const FRenderingCompositePassContext& Context,
		const FIntRect& OutputRect,
		FRHIUnorderedAccessView* OutUAV) const;

	const ESSAOType AOType;
	const bool bDirectOutput;
};

// ePId_Input0: defines the resolution we compute AO and provides the normal (only needed if bInAOSetupAsInput)
// ePId_Input1: setup in same resolution as ePId_Input1 for depth expect when running in full resolution, then it's half (only needed if bInAOSetupAsInput)
// ePId_Input2: optional AO result one lower resolution
// ePId_Input3: optional HZB
// derives from TRenderingCompositePassBase<InputCount, OutputCount> 
class FRCPassPostProcessAmbientOcclusion : public TRenderingCompositePassBase<4, 1>
{
public:
	// @param bInAOSetupAsInput true:use AO setup as input, false: use GBuffer normal and native z depth
	FRCPassPostProcessAmbientOcclusion(const FSceneView& View, ESSAOType InAOType, bool bInAOSetupAsInput = true, bool bInForcecIntermediateOutput = false, EPixelFormat InIntermediateFormatOverride = PF_Unknown);

	// interface FRenderingCompositePass ---------
	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual void Release() override { delete this; }
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;

private:
	
	void ProcessCS(FRenderingCompositePassContext& Context, const FSceneRenderTargetItem* DestRenderTarget, const FIntRect& ViewRect, const FIntPoint& TexSize, int32 ShaderQuality, bool bDoUpsample);
	void ProcessPS(FRenderingCompositePassContext& Context, const FSceneRenderTargetItem* DestRenderTarget, const FSceneRenderTargetItem* SceneDepthBuffer, const FIntRect& ViewRect, const FIntPoint& TexSize, int32 ShaderQuality, bool bDoUpsample);

	template <uint32 bAOSetupAsInput, uint32 bDoUpsample, uint32 SampleSetQuality>
	FShader* SetShaderTemplPS(const FRenderingCompositePassContext& Context, FGraphicsPipelineStateInitializer& GraphicsPSOInit);

	template <uint32 bAOSetupAsInput, uint32 bDoUpsample, uint32 SampleSetQuality, typename TRHICmdList>
	void DispatchCS(TRHICmdList& RHICmdList, const FRenderingCompositePassContext& Context, const FIntPoint& TexSize, FRHIUnorderedAccessView* OutTextureUAV);
	
	const ESSAOType AOType;
	const EPixelFormat IntermediateFormatOverride;
	const bool bAOSetupAsInput;
	const bool bForceIntermediateOutput;
};


class FRCPassPostProcessAmbientOcclusion_HorizonSearch : public TRenderingCompositePassBase<2, 2>
{
public:

	FRCPassPostProcessAmbientOcclusion_HorizonSearch(const FSceneView& View, uint32 DownScaleFactor, const EGTAOType AOType );

	// interface FRenderingCompositePass ---------
	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual void Release() override { delete this; }
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;

	template <uint32 ShaderQuality>
	void DispatchCS(const FRenderingCompositePassContext& Context, FIntRect ViewRect, FIntPoint DestSize, FIntPoint TexSize);

	template <uint32 ShaderQuality>
	FShader* SetShaderPS(const FRenderingCompositePassContext& Context, FGraphicsPipelineStateInitializer& GraphicsPSOInit, FIntPoint DestSize);

private:
	const EGTAOType AOType;
	uint32		DownScaleFactor;
};


class FRCPassPostProcessAmbientOcclusion_GTAOInnerIntegrate : public TRenderingCompositePassBase<2, 1>
{
public:

	FRCPassPostProcessAmbientOcclusion_GTAOInnerIntegrate(const FSceneView& View, uint32 DownScaleFactor, bool FinalOutput);

	// interface FRenderingCompositePass ---------
	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual void Release() override { delete this; }
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;

private:
	const bool bFinalOutput;
	uint32		DownScaleFactor;
};

class FRCPassPostProcessAmbientOcclusion_GTAOHorizonSearchIntegrate : public TRenderingCompositePassBase<2, 2>
{
public:

	FRCPassPostProcessAmbientOcclusion_GTAOHorizonSearchIntegrate(const FSceneView& View, uint32 DownScaleFactor, bool FinalOutput, const EGTAOType AOType);

	// interface FRenderingCompositePass ---------
	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual void Release() override { delete this; }
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;

	template <uint32 ShaderQuality, uint32 UseNormals, typename TRHICmdList>
	void DispatchCS(TRHICmdList& RHICmdList, const FRenderingCompositePassContext& Context, FIntRect ViewRect, FIntPoint DestSize, FIntPoint TexSize);

	template <uint32 ShaderQuality, uint32 UseNormals>
	FShader* SetShaderPS(const FRenderingCompositePassContext& Context, FGraphicsPipelineStateInitializer& GraphicsPSOInit, FIntPoint DestSize);

private:
	const EGTAOType AOType;
	const bool bFinalOutput;
	uint32		DownScaleFactor;
};




class FRCPassPostProcessAmbientOcclusion_GTAO_TemporalFilter : public TRenderingCompositePassBase<1, 3>
{
public:

	FRCPassPostProcessAmbientOcclusion_GTAO_TemporalFilter(const FSceneView& View, uint32 DownScaleFactor, const FGTAOTAAHistory& InInputHistory, FGTAOTAAHistory* OutOutputHistory, const EGTAOType AOType);

	// interface FRenderingCompositePass ---------
	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual void Release() override { delete this; }
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;

	template <uint32 ShaderQuality>
	void DispatchCS(const FRenderingCompositePassContext& Context, FIntRect OutputViewRect, FIntPoint OutputTexSize);

private:
	const EGTAOType AOType;
	const FGTAOTAAHistory& InputHistory;
	FGTAOTAAHistory* OutputHistory;
	uint32		DownScaleFactor;
};

class FRCPassPostProcessAmbientOcclusion_GTAO_SpatialFilter : public TRenderingCompositePassBase<2, 1>
{
public:

	FRCPassPostProcessAmbientOcclusion_GTAO_SpatialFilter(const FSceneView& View, uint32 DownScaleFactor, const EGTAOType AOType);

	// interface FRenderingCompositePass ---------
	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual void Release() override { delete this; }
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;

private:
	const EGTAOType AOType;
	uint32		DownScaleFactor;
};

class FRCPassPostProcessAmbientOcclusion_GTAO_Upsample : public TRenderingCompositePassBase<2, 1>
{
public:

	FRCPassPostProcessAmbientOcclusion_GTAO_Upsample(const FSceneView& View, uint32 DownScaleFactor, const EGTAOType AOType);

	// interface FRenderingCompositePass ---------
	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual void Release() override { delete this; }
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;

private:
	const EGTAOType AOType;
	uint32			DownScaleFactor;
};