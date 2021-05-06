// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXPixelShader.h: AGX RHI Pixel Shader Class Definition.
=============================================================================*/

#pragma once


//------------------------------------------------------------------------------

#pragma mark - AGX RHI Pixel Shader Class


class FAGXPixelShader : public TAGXBaseShader<FRHIPixelShader, SF_Pixel>
{
public:
	FAGXPixelShader(TArrayView<const uint8> InCode);
	FAGXPixelShader(TArrayView<const uint8> InCode, mtlpp::Library InLibrary);

	mtlpp::Function GetFunction();
};
