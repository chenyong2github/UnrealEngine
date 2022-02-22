// Copyright Epic Games, Inc. All Rights Reserved.

// The ShaderPrint system uses a RWBuffer to capture any debug print from a shader.
// This means that the buffer needs to be bound for the shader you wish to debug.
// It would be ideal if that was automatic (maybe by having a fixed bind point for the buffer and binding it for the entire view).
// But for now you need to manually add binding information to your FShader class.
// To do this either:
// (i) Use SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters) in your FShader::FParameters declaration and call SetParameters().

// Also it seems that we can only bind a RWBuffer to compute shaders right now. Fixing this would allow us to use this system from all shader stages.

#pragma once

#include "CoreMinimal.h"
#include "ShaderParameterMacros.h"
#include "ShaderParameters.h"

class FViewInfo;
struct FShaderPrintData;

namespace ShaderPrint
{
	// ShaderPrint uniform buffer layout
	BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FShaderPrintCommonParameters, RENDERER_API)
		SHADER_PARAMETER(FVector2f, FontSize)
		SHADER_PARAMETER(FVector2f, FontSpacing)
		SHADER_PARAMETER(FIntPoint, Resolution)
		SHADER_PARAMETER(FIntPoint, CursorCoord)
		SHADER_PARAMETER(int32, MaxValueCount)
		SHADER_PARAMETER(int32, MaxSymbolCount)
		SHADER_PARAMETER(uint32, MaxStateCount)
	END_GLOBAL_SHADER_PARAMETER_STRUCT()

	// ShaderPrint parameter struct declaration
	BEGIN_SHADER_PARAMETER_STRUCT(FShaderParameters, )
		SHADER_PARAMETER_STRUCT_REF(FShaderPrintCommonParameters, Common)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint2>, ShaderPrint_StateBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<ShaderPrintItem>, ShaderPrint_RWValuesBuffer)
	END_SHADER_PARAMETER_STRUCT()

	// Call this to fill the FShaderParameters
	RENDERER_API void SetParameters(FRDGBuilder& GraphBuilder, const FViewInfo & View, FShaderParameters& OutParameters);
	RENDERER_API void SetParameters(FRDGBuilder& GraphBuilder, const FShaderPrintData& Data, FShaderParameters& OutParameters);

	/**
	 * Call to ensure enough space for some number of characters, is added cumulatively each frame, to make it possible for several systems to request a certain number independently.
	 * Is used to grow the max element count for subsequent frames (as the allocation happens early in the frame).
	 * @param The number of elements requested, an element corresponds to a line, so a cube, for example, needs 12 elements.
	 */
	RENDERER_API void RequestSpaceForCharacters(uint32 MaxElementCount);
}

struct FShaderPrintData
{
	FVector2f FontSpacing;
	FVector2f FontSize;
	FIntPoint CursorCoord;
	FIntRect OutputRect;
	int32 MaxValueCount = -1;
	int32 MaxSymbolCount = -1;
	uint32 MaxStateCount = 0;

	FRDGBufferRef ShaderPrintValueBuffer = nullptr;
	FRDGBufferRef ShaderPrintStateBuffer = nullptr;
	TUniformBufferRef<ShaderPrint::FShaderPrintCommonParameters> UniformBuffer;
};