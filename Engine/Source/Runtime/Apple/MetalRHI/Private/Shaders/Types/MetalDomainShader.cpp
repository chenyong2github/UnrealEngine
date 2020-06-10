// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalDomainShader.cpp: Metal RHI Domain Shader Class Implementation.
=============================================================================*/


#include "MetalRHIPrivate.h"
#include "Templates/MetalBaseShader.h"
#include "MetalDomainShader.h"


//------------------------------------------------------------------------------

#pragma mark - Metal RHI Domain Shader Class


#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
FMetalDomainShader::FMetalDomainShader(TArrayView<const uint8> InCode)
{
	FMetalCodeHeader Header;
	Init(InCode, Header);

	// for VSHS
	auto const& Tess = Header.Tessellation[0];
	TessellationHSOutBuffer = Tess.TessellationHSOutBuffer;
	TessellationControlPointOutBuffer = Tess.TessellationControlPointOutBuffer;

	switch (Tess.TessellationOutputWinding)
	{
		// NOTE: cw and ccw are flipped
		case EMetalOutputWindingMode::Clockwise:		TessellationOutputWinding = mtlpp::Winding::CounterClockwise; break;
		case EMetalOutputWindingMode::CounterClockwise:	TessellationOutputWinding = mtlpp::Winding::Clockwise; break;
		default: check(0);
	}

	switch (Tess.TessellationPartitioning)
	{
		case EMetalPartitionMode::Pow2:				TessellationPartitioning = mtlpp::TessellationPartitionMode::ModePow2; break;
		case EMetalPartitionMode::Integer:			TessellationPartitioning = mtlpp::TessellationPartitionMode::ModeInteger; break;
		case EMetalPartitionMode::FractionalOdd:	TessellationPartitioning = mtlpp::TessellationPartitionMode::ModeFractionalOdd; break;
		case EMetalPartitionMode::FractionalEven:	TessellationPartitioning = mtlpp::TessellationPartitionMode::ModeFractionalEven; break;
		default: check(0);
	}

	TessellationDomain = Tess.TessellationDomain;
	TessellationOutputAttribs = Tess.TessellationOutputAttribs;
}

FMetalDomainShader::FMetalDomainShader(TArrayView<const uint8> InCode, mtlpp::Library InLibrary)
{
	FMetalCodeHeader Header;
	Init(InCode, Header, InLibrary);

	// for VSHS
	auto const& Tess = Header.Tessellation[0];
	TessellationHSOutBuffer = Tess.TessellationHSOutBuffer;
	TessellationControlPointOutBuffer = Tess.TessellationControlPointOutBuffer;

	switch (Tess.TessellationOutputWinding)
	{
		// NOTE: cw and ccw are flipped
		case EMetalOutputWindingMode::Clockwise:		TessellationOutputWinding = mtlpp::Winding::CounterClockwise; break;
		case EMetalOutputWindingMode::CounterClockwise:	TessellationOutputWinding = mtlpp::Winding::Clockwise; break;
		default: check(0);
	}

	switch (Tess.TessellationPartitioning)
	{
		case EMetalPartitionMode::Pow2:				TessellationPartitioning = mtlpp::TessellationPartitionMode::ModePow2; break;
		case EMetalPartitionMode::Integer:			TessellationPartitioning = mtlpp::TessellationPartitionMode::ModeInteger; break;
		case EMetalPartitionMode::FractionalOdd:	TessellationPartitioning = mtlpp::TessellationPartitionMode::ModeFractionalOdd; break;
		case EMetalPartitionMode::FractionalEven:	TessellationPartitioning = mtlpp::TessellationPartitionMode::ModeFractionalEven; break;
		default: check(0);
	}

	TessellationDomain = Tess.TessellationDomain;
	TessellationOutputAttribs = Tess.TessellationOutputAttribs;
}

mtlpp::Function FMetalDomainShader::GetFunction()
{
	return GetCompiledFunction();
}
#endif // PLATFORM_SUPPORTS_TESSELLATION_SHADERS
