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
	Init(InCode, Header);
}

FAGXVertexShader::FAGXVertexShader(TArrayView<const uint8> InCode, mtlpp::Library InLibrary)
{
	FMetalCodeHeader Header;
	Init(InCode, Header, InLibrary);
}

mtlpp::Function FAGXVertexShader::GetFunction()
{
	return GetCompiledFunction();
}
