// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalVertexShader.h: Metal RHI Vertex Shader Class Definition.
=============================================================================*/

#pragma once


//------------------------------------------------------------------------------

#pragma mark - Metal RHI Vertex Shader Class


class FMetalVertexShader : public TMetalBaseShader<FRHIVertexShader, SF_Vertex>
{
public:
	FMetalVertexShader(TArrayView<const uint8> InCode);
	FMetalVertexShader(TArrayView<const uint8> InCode, mtlpp::Library InLibrary);

	mtlpp::Function GetFunction();

#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
	// for VSHS
	FMetalTessellationOutputs TessellationOutputAttribs;
	float  TessellationMaxTessFactor;
	uint32 TessellationOutputControlPoints;
	uint32 TessellationDomain;
	uint32 TessellationInputControlPoints;
	uint32 TessellationPatchesPerThreadGroup;
	uint32 TessellationPatchCountBuffer;
	uint32 TessellationIndexBuffer;
	uint32 TessellationHSOutBuffer;
	uint32 TessellationHSTFOutBuffer;
	uint32 TessellationControlPointOutBuffer;
	uint32 TessellationControlPointIndexBuffer;
#endif // PLATFORM_SUPPORTS_TESSELLATION_SHADERS
};
