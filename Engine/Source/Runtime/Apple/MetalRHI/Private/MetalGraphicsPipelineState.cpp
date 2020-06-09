// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalGraphicsPipelineState.cpp: Metal RHI graphics pipeline state class.
=============================================================================*/


#include "MetalRHIPrivate.h"
#include "MetalVertexDeclaration.h"
#include "MetalShaderTypes.h"
#include "MetalGraphicsPipelineState.h"


//------------------------------------------------------------------------------

#pragma mark - Metal Graphics Pipeline State Support Routines


// From MetalPipeline.cpp:
extern FMetalShaderPipeline* GetMTLRenderPipeline(bool const bSync, FMetalGraphicsPipelineState const* State, const FGraphicsPipelineStateInitializer& Init, EMetalIndexType const IndexType);
extern void ReleaseMTLRenderPipeline(FMetalShaderPipeline* Pipeline);


//------------------------------------------------------------------------------

#pragma mark - Metal Graphics Pipeline State Class Implementation


FMetalGraphicsPipelineState::FMetalGraphicsPipelineState(const FGraphicsPipelineStateInitializer& Init)
	: Initializer(Init)
{
	// void
}

bool FMetalGraphicsPipelineState::Compile()
{
	FMemory::Memzero(PipelineStates);

	for (uint32 i = 0; i < EMetalIndexType_Num; i++)
	{
		PipelineStates[i] = [GetMTLRenderPipeline(true, this, Initializer, (EMetalIndexType)i) retain];
		if(!PipelineStates[i])
		{
			return false;
		}
	}

	return true;
}

FMetalGraphicsPipelineState::~FMetalGraphicsPipelineState()
{
	for (uint32 i = 0; i < EMetalIndexType_Num; i++)
	{
		ReleaseMTLRenderPipeline(PipelineStates[i]);
		PipelineStates[i] = nil;
	}
}

FMetalShaderPipeline* FMetalGraphicsPipelineState::GetPipeline(EMetalIndexType IndexType)
{
	check(IndexType < EMetalIndexType_Num);

	if (!PipelineStates[IndexType])
	{
		PipelineStates[IndexType] = [GetMTLRenderPipeline(true, this, Initializer, IndexType) retain];
	}

	FMetalShaderPipeline* Pipe = PipelineStates[IndexType];
	check(Pipe);

    return Pipe;
}
