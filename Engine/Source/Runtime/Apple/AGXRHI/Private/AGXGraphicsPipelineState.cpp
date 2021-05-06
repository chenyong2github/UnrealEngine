// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXGraphicsPipelineState.cpp: AGX RHI graphics pipeline state class.
=============================================================================*/


#include "AGXRHIPrivate.h"
#include "AGXVertexDeclaration.h"
#include "AGXShaderTypes.h"
#include "AGXGraphicsPipelineState.h"


//------------------------------------------------------------------------------

#pragma mark - AGX RHI Graphics Pipeline State Support Routines


// From AGXPipeline.cpp:
extern FAGXShaderPipeline* GetMTLRenderPipeline(bool const bSync, FAGXGraphicsPipelineState const* State, const FGraphicsPipelineStateInitializer& Init);
extern void ReleaseMTLRenderPipeline(FAGXShaderPipeline* Pipeline);


//------------------------------------------------------------------------------

#pragma mark - AGX RHI Graphics Pipeline State Class Implementation


FAGXGraphicsPipelineState::FAGXGraphicsPipelineState(const FGraphicsPipelineStateInitializer& Init)
	: Initializer(Init)
	, PipelineState(nil)
{
	// void
}

bool FAGXGraphicsPipelineState::Compile()
{
	check(PipelineState == nil);
	PipelineState = [GetMTLRenderPipeline(true, this, Initializer) retain];
	return (PipelineState != nil);
}

FAGXGraphicsPipelineState::~FAGXGraphicsPipelineState()
{
	if (PipelineState != nil)
	{
		ReleaseMTLRenderPipeline(PipelineState);
		PipelineState = nil;
	}
}

FAGXShaderPipeline* FAGXGraphicsPipelineState::GetPipeline()
{
	if (PipelineState == nil)
	{
		PipelineState = [GetMTLRenderPipeline(true, this, Initializer) retain];
		check(PipelineState != nil);
	}

    return PipelineState;
}
