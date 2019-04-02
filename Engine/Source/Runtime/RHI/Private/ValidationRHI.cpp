// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ValidationRHI.cpp: Public Valdation RHI definitions.
=============================================================================*/

#include "ValidationRHI.h"

#if ENABLE_RHI_VALIDATION
#include "RHI.h"
#include "ValidationContext.h"

FValidationRHI* GValidationRHI = nullptr;

FValidationRHI::FValidationRHI(FDynamicRHI* InRHI)
	: RHI(InRHI)
{
	check(RHI);

	Context = new FValidationContext(this);
	AsyncComputeContext = new FValidationComputeContext(this);
}

FValidationRHI::~FValidationRHI()
{
	delete AsyncComputeContext;
	delete Context;
}

IRHICommandContext* FValidationRHI::RHIGetDefaultContext()
{
	if (!Context->RHIContext)
	{
		Context->RHIContext = RHI->RHIGetDefaultContext();
	}

	return Context;
}

IRHIComputeContext* FValidationRHI::RHIGetDefaultAsyncComputeContext()
{
	if (!AsyncComputeContext->RHIContext)
	{
		AsyncComputeContext->RHIContext = RHI->RHIGetDefaultAsyncComputeContext();
	}

	return AsyncComputeContext;
}


FValidationComputeContext::FValidationComputeContext(FValidationRHI* InRHI)
	: RHIContext(nullptr)
	, RHI(InRHI)
{
	State.Reset();
}

void FValidationComputeContext::FState::Reset()
{
	ComputePassName.Reset();
	bComputeShaderSet = false;
}


FValidationContext::FValidationContext(FValidationRHI* InRHI)
	: RHIContext(nullptr)
	, RHI(InRHI)
{
	State.Reset();
}

void FValidationContext::FState::Reset()
{
	bInsideBeginRenderPass = false;
	bGfxPSOSet = false;
	RenderPassName.Reset();
	PreviousRenderPassName.Reset();
	bInsideComputePass = false;
	ComputePassName.Reset();
	bComputeShaderSet = false;
}
#endif	// ENABLE_RHI_VALIDATION
