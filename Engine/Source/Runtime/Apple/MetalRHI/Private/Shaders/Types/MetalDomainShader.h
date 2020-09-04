// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalDomainShader.h: Metal RHI Domain Shader Class Definition.
=============================================================================*/

#pragma once


//------------------------------------------------------------------------------

#pragma mark - Metal RHI Domain Shader Class


class FMetalDomainShader : public TMetalBaseShader<FRHIDomainShader, SF_Domain>
{
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
public:
	FMetalDomainShader(TArrayView<const uint8> InCode);
	FMetalDomainShader(TArrayView<const uint8> InCode, mtlpp::Library InLibrary);

	mtlpp::Function GetFunction();

	mtlpp::Winding TessellationOutputWinding;
	mtlpp::TessellationPartitionMode TessellationPartitioning;
	uint32 TessellationHSOutBuffer;
	uint32 TessellationControlPointOutBuffer;

	uint32 TessellationDomain;
	FMetalTessellationOutputs TessellationOutputAttribs;
#endif // PLATFORM_SUPPORTS_TESSELLATION_SHADERS
};
