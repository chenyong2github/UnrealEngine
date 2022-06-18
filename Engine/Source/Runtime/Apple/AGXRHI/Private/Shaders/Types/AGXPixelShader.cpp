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
	TRefCountPtr<FMTLLibrary> Library;
	Init(InCode, Header, Library);
}

FAGXPixelShader::FAGXPixelShader(TArrayView<const uint8> InCode, const TRefCountPtr<FMTLLibrary>& InLibrary)
{
	FMetalCodeHeader Header;
	Init(InCode, Header, InLibrary);
}

id<MTLFunction> FAGXPixelShader::GetFunction()
{
	return GetCompiledFunction();
}
