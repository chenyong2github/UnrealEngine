// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXComputePipelineState.h: AGX RHI compute pipeline state class.
=============================================================================*/

#pragma once

class FAGXComputePipelineState : public FRHIComputePipelineState
{
public:
	FAGXComputePipelineState(FAGXComputeShader* InComputeShader)
		: ComputeShader(InComputeShader)
	{
		check(InComputeShader);
	}

	virtual ~FAGXComputePipelineState()
	{
		// void
	}

	FAGXComputeShader* GetComputeShader()
	{
		return ComputeShader;
	}

private:
	TRefCountPtr<FAGXComputeShader> ComputeShader;
};
