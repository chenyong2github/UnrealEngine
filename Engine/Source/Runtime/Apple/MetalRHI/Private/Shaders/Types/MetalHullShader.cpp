// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalHullShader.cpp: Metal RHI Hull Shader Class Implementation.
=============================================================================*/


#include "MetalRHIPrivate.h"
#include "Templates/MetalBaseShader.h"
#include "MetalHullShader.h"


//------------------------------------------------------------------------------

#pragma mark - Metal RHI Hull Shader Class


#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
FMetalHullShader::FMetalHullShader(TArrayView<const uint8> InCode)
{
	FMetalCodeHeader Header;
	Init(InCode, Header);

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

		switch (Tess.TessellationOutputWinding)
		{
				// NOTE: cw and ccw are flipped
			case EMetalOutputWindingMode::Clockwise:		TessellationOutputWinding = mtlpp::Winding::CounterClockwise; break;
			case EMetalOutputWindingMode::CounterClockwise:	TessellationOutputWinding = mtlpp::Winding::Clockwise; break;
			default: break;
		}

		switch (Tess.TessellationPartitioning)
		{
			case EMetalPartitionMode::Pow2:				TessellationPartitioning = mtlpp::TessellationPartitionMode::ModePow2; break;
			case EMetalPartitionMode::Integer:			TessellationPartitioning = mtlpp::TessellationPartitionMode::ModeInteger; break;
			case EMetalPartitionMode::FractionalOdd:	TessellationPartitioning = mtlpp::TessellationPartitionMode::ModeFractionalOdd; break;
			case EMetalPartitionMode::FractionalEven:	TessellationPartitioning = mtlpp::TessellationPartitionMode::ModeFractionalEven; break;
			default: break;
		}
	}
}

FMetalHullShader::FMetalHullShader(TArrayView<const uint8> InCode, mtlpp::Library InLibrary)
{
	FMetalCodeHeader Header;
	Init(InCode, Header, InLibrary);

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

		switch (Tess.TessellationOutputWinding)
		{
				// NOTE: cw and ccw are flipped
			case EMetalOutputWindingMode::Clockwise:		TessellationOutputWinding = mtlpp::Winding::CounterClockwise; break;
			case EMetalOutputWindingMode::CounterClockwise:	TessellationOutputWinding = mtlpp::Winding::Clockwise; break;
			default: break;
		}

		switch (Tess.TessellationPartitioning)
		{
			case EMetalPartitionMode::Pow2:				TessellationPartitioning = mtlpp::TessellationPartitionMode::ModePow2; break;
			case EMetalPartitionMode::Integer:			TessellationPartitioning = mtlpp::TessellationPartitionMode::ModeInteger; break;
			case EMetalPartitionMode::FractionalOdd:	TessellationPartitioning = mtlpp::TessellationPartitionMode::ModeFractionalOdd; break;
			case EMetalPartitionMode::FractionalEven:	TessellationPartitioning = mtlpp::TessellationPartitionMode::ModeFractionalEven; break;
			default: break;
		}
	}
}

mtlpp::Function FMetalHullShader::GetFunction()
{
	return GetCompiledFunction();
}
#endif // PLATFORM_SUPPORTS_TESSELLATION_SHADERS
