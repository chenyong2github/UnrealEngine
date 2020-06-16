// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalHullShader.h: Metal RHI Hull Shader Class Definition.
=============================================================================*/

#pragma once


//------------------------------------------------------------------------------

#pragma mark - Metal RHI Hull Shader Class


class FMetalHullShader : public TMetalBaseShader<FRHIHullShader, SF_Hull>
{
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
public:
	FMetalHullShader(TArrayView<const uint8> InCode);
	FMetalHullShader(TArrayView<const uint8> InCode, mtlpp::Library InLibrary);

	mtlpp::Function GetFunction();

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
	mtlpp::Winding TessellationOutputWinding;
	mtlpp::TessellationPartitionMode TessellationPartitioning;
#endif // PLATFORM_SUPPORTS_TESSELLATION_SHADERS
};
