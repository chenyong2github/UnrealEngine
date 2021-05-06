// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXPixelShader.cpp: AGX RHI Pixel Shader Class Implementation.
=============================================================================*/


#include "AGXRHIPrivate.h"
#include "Templates/AGXBaseShader.h"
#include "AGXPixelShader.h"


//------------------------------------------------------------------------------

#pragma mark - AGX RHI Pixel Shader Class


FAGXPixelShader::FAGXPixelShader(TArrayView<const uint8> InCode)
{
	FMetalCodeHeader Header;
	Init(InCode, Header);
}

FAGXPixelShader::FAGXPixelShader(TArrayView<const uint8> InCode, mtlpp::Library InLibrary)
{
	FMetalCodeHeader Header;
	Init(InCode, Header, InLibrary);
}

mtlpp::Function FAGXPixelShader::GetFunction()
{
	return GetCompiledFunction();
}
