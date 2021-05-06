// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXVertexShader.h: AGX RHI Vertex Shader Class Definition.
=============================================================================*/

#pragma once


//------------------------------------------------------------------------------

#pragma mark - AGX RHI Vertex Shader Class


class FAGXVertexShader : public TAGXBaseShader<FRHIVertexShader, SF_Vertex>
{
public:
	FAGXVertexShader(TArrayView<const uint8> InCode);
	FAGXVertexShader(TArrayView<const uint8> InCode, mtlpp::Library InLibrary);

	mtlpp::Function GetFunction();
};
