// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHI.h"
#include "ScreenRendering.h"
#include "CommonRenderResources.h"

class NIAGARASHADER_API FNiagaraVisualizeTexture2DPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FNiagaraVisualizeTexture2DPS, Global);

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	FNiagaraVisualizeTexture2DPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer);
	FNiagaraVisualizeTexture2DPS() {}

	void SetParameters(FRHICommandList& RHICmdList, FIntVector4 AttributesToVisualize, FRHITexture* Texture, FIntPoint NumTextureAttributes, uint32 TickCounter);

private:
	LAYOUT_FIELD(FShaderResourceParameter, Texture2DParam);
	LAYOUT_FIELD(FShaderResourceParameter, TextureSamplerParam);
	LAYOUT_FIELD(FShaderParameter, NumTextureAttributesParam);
	LAYOUT_FIELD(FShaderParameter, NumAttributesToVisualizeParam);
	LAYOUT_FIELD(FShaderParameter, AttributesToVisualizeParam);
	LAYOUT_FIELD(FShaderParameter, DebugParamsParam);
};
