// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXComputeShader.h: AGX RHI Compute Shader Class Definition.
=============================================================================*/

#pragma once


//------------------------------------------------------------------------------

#pragma mark - AGX RHI Compute Shader Class


class FAGXComputeShader : public TAGXBaseShader<FRHIComputeShader, SF_Compute>
{
public:
	FAGXComputeShader(TArrayView<const uint8> InCode, const TRefCountPtr<FMTLLibrary>& InLibrary);
	virtual ~FAGXComputeShader();

	FAGXShaderPipeline* GetPipeline();
	id<MTLFunction> GetFunction();

	// thread group counts
	int32 NumThreadsX;
	int32 NumThreadsY;
	int32 NumThreadsZ;

private:
	// the state object for a compute shader
	FAGXShaderPipeline* Pipeline;
};
