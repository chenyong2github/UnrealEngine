// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RendererInterface.h"
#include "RHICommandList.h"
#include "RenderGraph.h"

class FViewInfo;

struct FShaderDrawDebugData
{
	FIntPoint CursorPosition;
	TRefCountPtr<FPooledRDGBuffer> Buffer;
	TRefCountPtr<FPooledRDGBuffer> IndirectBuffer;
};

namespace ShaderDrawDebug 
{
	// Call this to know if this is even just available (for exemple in shipping mode buffers won't exists)
	RENDERER_API bool IsShaderDrawDebugEnabled();

	// Call this to know if a view can render this debug information
	bool IsShaderDrawDebugEnabled(const FViewInfo& View);

	// Allocate the debug print buffer associated with the view
	void BeginView(FRHICommandListImmediate& RHICmdList, FViewInfo& View);
	// Draw info from the debug print buffer to the given output target
	void DrawView(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef OutputTexture, FRDGTextureRef DepthTexture);
	// Release the debug print buffer associated with the view
	void EndView(FViewInfo& View);

	// The structure to be set on the debug shader outputting debug primitive
	BEGIN_SHADER_PARAMETER_STRUCT(FShaderDrawDebugParameters, )
		SHADER_PARAMETER(FIntPoint, ShaderDrawCursorPos)
		SHADER_PARAMETER(int32, ShaderDrawMaxElementCount)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer, OutShaderDrawPrimitive)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, OutputShaderDrawIndirect)
	END_SHADER_PARAMETER_STRUCT()

	// Call this to fill the FShaderDrawParameters
	RENDERER_API void SetParameters(FRDGBuilder& GraphBuilder, const FShaderDrawDebugData& Data, FShaderDrawDebugParameters& OutParameters);

}
