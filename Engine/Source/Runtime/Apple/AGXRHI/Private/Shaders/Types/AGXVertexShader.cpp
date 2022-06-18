// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXVertexShader.cpp: AGX RHI Vertex Shader Class Implementation.
=============================================================================*/


#include "AGXRHIPrivate.h"
#include "Templates/AGXBaseShader.h"
#include "AGXVertexShader.h"


//------------------------------------------------------------------------------

#pragma mark - AGX RHI Vertex Shader Class


FAGXVertexShader::FAGXVertexShader(TArrayView<const uint8> InCode)
{
	FMetalCodeHeader Header;
	TRefCountPtr<FMTLLibrary> Library;
	Init(InCode, Header, Library);
}

FAGXVertexShader::FAGXVertexShader(TArrayView<const uint8> InCode, const TRefCountPtr<FMTLLibrary>& InLibrary)
{
	FMetalCodeHeader Header;
	Init(InCode, Header, InLibrary);
}

id<MTLFunction> FAGXVertexShader::GetFunction()
{
	return GetCompiledFunction();
}
