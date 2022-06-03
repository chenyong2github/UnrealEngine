// Copyright Epic Games, Inc. All Rights Reserved.

// The ShaderPrint system uses a RWBuffer to capture any debug print from a shader.
// This means that the buffer needs to be bound for the shader you wish to debug.
// It would be ideal if that was automatic (maybe by having a fixed bind point for the buffer and binding it for the entire view).
// But for now you need to manually add binding information to your FShader class.
// To do this use SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters) in your FShader::FParameters declaration.
// Then call a variant of SetParameters().

#pragma once

#include "CoreMinimal.h"
#include "RenderGraphDefinitions.h"
#include "ShaderParameterMacros.h"
#include "ShaderParameters.h"

class FSceneView;
struct FShaderPrintData;
class FRDGBuilder;
class FViewInfo;

namespace ShaderPrint
{
	// ShaderPrint uniform buffer layout
	BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FShaderPrintCommonParameters, RENDERER_API)
		SHADER_PARAMETER(FIntPoint, Resolution)
		SHADER_PARAMETER(FIntPoint, CursorCoord)
		SHADER_PARAMETER(FVector3f, TranslatedWorldOffset)
		SHADER_PARAMETER(FVector2f, FontSize)
		SHADER_PARAMETER(FVector2f, FontSpacing)
		SHADER_PARAMETER(uint32, MaxValueCount)
		SHADER_PARAMETER(uint32, MaxSymbolCount)
		SHADER_PARAMETER(uint32, MaxStateCount)
		SHADER_PARAMETER(uint32, MaxLineCount)
		SHADER_PARAMETER(uint32, MaxTriangleCount)
	END_GLOBAL_SHADER_PARAMETER_STRUCT()

	// ShaderPrint parameter struct declaration
	BEGIN_SHADER_PARAMETER_STRUCT(FShaderParameters, )
		SHADER_PARAMETER_STRUCT_REF(FShaderPrintCommonParameters, Common)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint2>, ShaderPrint_StateBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<ShaderPrintItem>, ShaderPrint_RWValuesBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, ShaderPrint_RWPrimitivesBuffer)
	END_SHADER_PARAMETER_STRUCT()

	// Does the platform support the ShaderPrint system?
	RENDERER_API bool IsSupported(const EShaderPlatform Platform);

	// Have we enabled the ShaderPrint system?
	RENDERER_API bool IsEnabled();

	// Call this to know if a view can render this debug information.
	// This should be checked before using a permutation that requires the shader draw parameters.
	RENDERER_API bool IsEnabled(const FSceneView& View);

	// Returns true if the default view exists and has shader debug rendering enabled.
	// This should be checked before using a permutation that requires the shader draw parameters.
	RENDERER_API bool IsDefaultViewEnabled();

	// Enable/disable shader print.
	RENDERER_API void SetEnabled(bool bInEnabled);

	// Set characters font size.
	RENDERER_API void SetFontSize(int32 InFontSize);

	/**
	 * Call to ensure enough space for some number of characters/lines, is added cumulatively each frame, to make 
	 * it possible for several systems to request a certain number independently.
	 * Is used to grow the max element count for subsequent frames (as the allocation happens early in the frame).
	 * @param The number of elements requested, an element corresponds to a line, so a cube, for example, needs 12 elements.
	 */
	RENDERER_API void RequestSpaceForCharacters(uint32 MaxElementCount);
	RENDERER_API void RequestSpaceForLines(uint32 MaxElementCount);
	RENDERER_API void RequestSpaceForTriangles(uint32 MaxElementCount);

	/** Structure containing setup for shader print capturing. */
	struct RENDERER_API FShaderPrintSetup
	{
		/** Construct with view and system defaults. */
		FShaderPrintSetup(FSceneView const& InView);
		/** Construct with view rectangle and system defaults. */
		FShaderPrintSetup(FIntRect InViewRect);

		/** Expected viewport rectangle. */
		FIntRect ViewRect;
		/** Cursor pixel position within viewport. Can be used for isolating a pixel to debug. */
		FIntPoint CursorCoord;
		/** PreView translation used for storing line positions in translated world space. */
		FVector PreViewTranslation;
		/** DPI scale to take into account when drawing font. */
		float DPIScale;
		/** Font size in pixels. */
		FIntPoint FontSize;
		/** Font spacing in pixels (not including font size). */
		FIntPoint FontSpacing;
		/** Initial size of character buffer. Will also be increased by RequestSpaceForCharacters(). */
		uint32 MaxValueCount;
		/** Initial size of widget buffer. */
		uint32 MaxStateCount;
		/** Initial size of line buffer. Will also be increased by RequestSpaceForLines(). */
		uint32 MaxLineCount;
		/** Initial size of triangle buffer. Will also be increased by RequestSpaceForLines(). */
		uint32 MaxTriangleCount;
	};

	/** Create the shader print render data. This allocates and clears the render buffers. */
	RENDERER_API FShaderPrintData CreateShaderPrintData(FRDGBuilder& GraphBuilder, FShaderPrintSetup const& InSetup);

	/** Fill the FShaderParameters with an explicit FShaderPrintData managed by the calling code. */
	RENDERER_API void SetParameters(FRDGBuilder& GraphBuilder, const FShaderPrintData& Data, FShaderParameters& OutParameters);
	/** Fill the FShaderParameters with the opaque FShaderPrintData from the current default view. */
	RENDERER_API void SetParameters(FRDGBuilder& GraphBuilder, FShaderParameters& OutParameters);
	
	UE_DEPRECATED(5.1, "Use one of the other implementations of SetParameters()")
	void SetParameters(FRDGBuilder& GraphBuilder, const FViewInfo& View, FShaderParameters& OutParameters);
}

/** 
 * Structure containing shader print render data.
 * This is automatically created, setup and rendered for each view.
 * Also it is possible for client code to create and own this. 
 * If this is client managed then the client also needs to send to the shader print renderer.
 */
struct RENDERER_API FShaderPrintData
{
	TUniformBufferRef<ShaderPrint::FShaderPrintCommonParameters> UniformBuffer;

	FRDGBufferRef ShaderPrintValueBuffer = nullptr;
	FRDGBufferRef ShaderPrintStateBuffer = nullptr;
	FRDGBufferRef ShaderPrintPrimitiveBuffer = nullptr;
};
