// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RendererInterface.h"
#include "RHICommandList.h"
#include "RenderGraph.h"

class FViewInfo;

struct FShaderDrawDebugData
{
	uint32 MaxElementCount = 0u;
	FIntPoint CursorPosition = FIntPoint(-1,-1);
	FVector ShaderDrawTranslatedWorldOffset;
	FRDGBufferRef Buffer = nullptr;
	bool IsEnabled() const { return MaxElementCount > 0; }
	bool IsValid() const { return Buffer != nullptr; }
};

namespace ShaderDrawDebug 
{
	// Call this to know if this is even just available (for exemple in shipping mode buffers won't exists)
	RENDERER_API bool IsEnabled();

	// Use to disable permutations that should not compile as the shaderdraw is unsupported.
	RENDERER_API bool IsSupported(const EShaderPlatform Platform);

	RENDERER_API void SetEnabled(bool bEnable);
	RENDERER_API void SetMaxElementCount(uint32 MaxCount);

	/**
	 * Call to ensure enough space for some number of elements, is added cumulatively each frame, to make it possible for several systems to request a certain number independently.
	 * Is used to grow the max element count for subsequent frames (as the allocation happens early in the frame). 
	 * @param The number of elements requested, an element corresponds to a line, so a cube, for example, needs 12 elements.
	 */
	RENDERER_API void RequestSpaceForElements(uint32 MaxElementCount);

	// Call this to know if a view can render this debug information
	bool IsEnabled(const FViewInfo& View);

	// Allocate the debug print buffer associated with the view
	void BeginView(FRDGBuilder& GraphBuilder, FViewInfo& View);
	// Draw info from the debug print buffer to the given output target
	void DrawView(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef OutputTexture, FRDGTextureRef DepthTexture);
	// Release the debug print buffer associated with the view
	void EndView(FViewInfo& View);

	// The structure to be set on the debug shader outputting debug primitive
	BEGIN_SHADER_PARAMETER_STRUCT(FShaderParameters, )
		SHADER_PARAMETER(FIntPoint, ShaderDrawCursorPos)
		SHADER_PARAMETER(int32, ShaderDrawMaxElementCount)
		SHADER_PARAMETER(FVector3f, ShaderDrawTranslatedWorldOffset)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer, OutShaderDrawPrimitive)
	END_SHADER_PARAMETER_STRUCT()

	// Call this to fill the FShaderDrawParameters
	RENDERER_API void SetParameters(FRDGBuilder& GraphBuilder, const FShaderDrawDebugData& Data, FShaderParameters& OutParameters);

	// Call this to fill the FShaderDrawParameters
	void SetParameters(FRDGBuilder& GraphBuilder, const FViewInfo& Data, FShaderParameters& OutParameters);

	// Returns true if the default view exists and has shader debug rendering enabled (this needs to be checked before using a permutation that requires the shader draw parameters)
	bool IsDefaultViewEnabled();

	// Call this to fill the FShaderDrawParameters using the default view (the first one for which Begin was called in case of stereo or similar)
	RENDERER_API void SetParameters(FRDGBuilder& GraphBuilder, FShaderParameters& OutParameters);
}
