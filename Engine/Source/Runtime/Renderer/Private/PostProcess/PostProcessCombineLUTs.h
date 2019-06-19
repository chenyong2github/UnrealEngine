// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessCombineLUTs.h: Post processing tone mapping implementation, can add bloom.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "ShaderParameters.h"
#include "PostProcess/RenderingCompositionGraph.h"

class UTexture;
class FFinalPostProcessSettings;

bool PipelineVolumeTextureLUTSupportGuaranteedAtRuntime(EShaderPlatform Platform);

// derives from TRenderingCompositePassBase<InputCount, OutputCount>
class FRCPassPostProcessCombineLUTs : public TRenderingCompositePassBase<0, 1>
{
public:
	FRCPassPostProcessCombineLUTs(EShaderPlatform InShaderPlatform, bool bInAllocateOutput, bool InIsComputePass, bool bInNeedFloatOutput)
	: ShaderPlatform(InShaderPlatform)
	, bAllocateOutput(bInAllocateOutput)
	, bNeedFloatOutput(bInNeedFloatOutput)
	{
		bIsComputePass = InIsComputePass;
		bPreferAsyncCompute = false;
	}

	// interface FRenderingCompositePass ---------
	virtual void Process(FRenderingCompositePassContext& Context) override;
	virtual void Release() override { delete this; }
	virtual FPooledRenderTargetDesc ComputeOutputDesc(EPassOutputId InPassOutputId) const override;

	/** */
	uint32 GenerateFinalTable(const FFinalPostProcessSettings& Settings, FTexture* OutTextures[], float OutWeights[], uint32 MaxCount) const;
	/** @return 0xffffffff if not found */
	uint32 FindIndex(const FFinalPostProcessSettings& Settings, UTexture* Tex) const;

	virtual FRHIComputeFence* GetComputePassEndFence() const override { return AsyncEndFence; }

private:
	template <typename TRHICmdList>
	void DispatchCS(TRHICmdList& RHICmdList, FRenderingCompositePassContext& Context, const FIntRect& DestRect, FRHIUnorderedAccessView* DestUAV, int32 BlendCount, FTexture* Textures[], float Weights[]);

	FComputeFenceRHIRef AsyncEndFence;

	EShaderPlatform ShaderPlatform;
	bool bAllocateOutput;
	bool bNeedFloatOutput;
};



/*-----------------------------------------------------------------------------
FColorRemapShaderParameters
-----------------------------------------------------------------------------*/

/** Encapsulates the color remap parameters. */
class FColorRemapShaderParameters
{
public:
	FColorRemapShaderParameters() {}

	FColorRemapShaderParameters(const FShaderParameterMap& ParameterMap);

	// Explicit declarations here because templates unresolved when used in other files
	void Set(FRHICommandList& RHICmdList, FRHIPixelShader* ShaderRHI);

	template <typename TRHICmdList>
	void Set(TRHICmdList& RHICmdList, FRHIComputeShader* ShaderRHI);

	friend FArchive& operator<<(FArchive& Ar,FColorRemapShaderParameters& P);

	FShaderParameter MappingPolynomial;
};