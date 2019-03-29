// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ValidationRHI.cpp: Public Valdation RHI definitions.
=============================================================================*/

#include "ValidationRHI.h"
#include "RHI.h"
#include "ValidationContext.h"


FValidationRHI::FValidationRHI(FDynamicRHI* InRHI)
	: RHI(InRHI)
{
	check(RHI);

	Context = new FValidationContext(this);
}

IRHICommandContext* FValidationRHI::RHIGetDefaultContext()
{
	if (!Context->RHIContext)
	{
		Context->RHIContext = RHI->RHIGetDefaultContext();
	}

	return Context;
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
