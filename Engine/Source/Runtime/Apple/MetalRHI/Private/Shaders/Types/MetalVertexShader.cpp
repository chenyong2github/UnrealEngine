// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalVertexShader.cpp: Metal RHI Vertex Shader Class Implementation.
=============================================================================*/


#include "MetalRHIPrivate.h"
#include "Templates/MetalBaseShader.h"
#include "MetalVertexShader.h"


//------------------------------------------------------------------------------

#pragma mark - Metal RHI Vertex Shader Class


FMetalVertexShader::FMetalVertexShader(TArrayView<const uint8> InCode)
{
	FMetalCodeHeader Header;
	Init(InCode, Header);

#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
	if (Header.Tessellation.Num())
	{
		auto const& Tess = Header.Tessellation[0];
		TessellationOutputAttribs = Tess.TessellationOutputAttribs;
		TessellationPatchCountBuffer = Tess.TessellationPatchCountBuffer;
		TessellationIndexBuffer = Tess.TessellationIndexBuffer;
		TessellationHSOutBuffer = Tess.TessellationHSOutBuffer;
		TessellationHSTFOutBuffer = Tess.TessellationHSTFOutBuffer;
		TessellationControlPointOutBuffer = Tess.TessellationControlPointOutBuffer;
		TessellationControlPointIndexBuffer = Tess.TessellationControlPointIndexBuffer;
		TessellationOutputControlPoints = Tess.TessellationOutputControlPoints;
		TessellationDomain = Tess.TessellationDomain;
		TessellationInputControlPoints = Tess.TessellationInputControlPoints;
		TessellationMaxTessFactor = Tess.TessellationMaxTessFactor;
		TessellationPatchesPerThreadGroup = Tess.TessellationPatchesPerThreadGroup;
	}
#endif // PLATFORM_SUPPORTS_TESSELLATION_SHADERS
}

FMetalVertexShader::FMetalVertexShader(TArrayView<const uint8> InCode, mtlpp::Library InLibrary)
{
	FMetalCodeHeader Header;
	Init(InCode, Header, InLibrary);

#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
	if (Header.Tessellation.Num())
	{
		auto const& Tess = Header.Tessellation[0];
		TessellationOutputAttribs = Tess.TessellationOutputAttribs;
		TessellationPatchCountBuffer = Tess.TessellationPatchCountBuffer;
		TessellationIndexBuffer = Tess.TessellationIndexBuffer;
		TessellationHSOutBuffer = Tess.TessellationHSOutBuffer;
		TessellationHSTFOutBuffer = Tess.TessellationHSTFOutBuffer;
		TessellationControlPointOutBuffer = Tess.TessellationControlPointOutBuffer;
		TessellationControlPointIndexBuffer = Tess.TessellationControlPointIndexBuffer;
		TessellationOutputControlPoints = Tess.TessellationOutputControlPoints;
		TessellationDomain = Tess.TessellationDomain;
		TessellationInputControlPoints = Tess.TessellationInputControlPoints;
		TessellationMaxTessFactor = Tess.TessellationMaxTessFactor;
		TessellationPatchesPerThreadGroup = Tess.TessellationPatchesPerThreadGroup;
	}
#endif // PLATFORM_SUPPORTS_TESSELLATION_SHADERS
}

mtlpp::Function FMetalVertexShader::GetFunction()
{
	return GetCompiledFunction();
}
