// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RHIValidation.cpp: Public RHI Validation layer definitions.
=============================================================================*/

#include "RHIValidation.h"

#if ENABLE_RHI_VALIDATION
#include "RHI.h"
#include "RHIValidationContext.h"

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

void FValidationRHI::ValidatePipeline(const FGraphicsPipelineStateInitializer& PSOInitializer)
{
	{
		// Verify depth/stencil access/usage
		bool bHasDepth = IsDepthOrStencilFormat(PSOInitializer.DepthStencilTargetFormat);
		bool bHasStencil = IsStencilFormat(PSOInitializer.DepthStencilTargetFormat);
		const FDepthStencilStateInitializerRHI& Initializer = GValidationRHI->DepthStencilStates.FindChecked(PSOInitializer.DepthStencilState);
		if (bHasDepth)
		{
			if (!bHasStencil)
			{
				ensureMsgf(!Initializer.bEnableFrontFaceStencil
					&& Initializer.FrontFaceStencilTest == CF_Always
					&& Initializer.FrontFaceStencilFailStencilOp == SO_Keep
					&& Initializer.FrontFaceDepthFailStencilOp == SO_Keep
					&& Initializer.FrontFacePassStencilOp == SO_Keep
					&& !Initializer.bEnableBackFaceStencil
					&& Initializer.BackFaceStencilTest == CF_Always
					&& Initializer.BackFaceStencilFailStencilOp == SO_Keep
					&& Initializer.BackFaceDepthFailStencilOp == SO_Keep
					&& Initializer.BackFacePassStencilOp == SO_Keep, TEXT("No stencil render target set, yet PSO wants to use stencil operations!"));
/*
				ensureMsgf(PSOInitializer.StencilTargetLoadAction == ERenderTargetLoadAction::ENoAction,
					TEXT("No stencil target set, yet PSO wants to load from it!"));
				ensureMsgf(PSOInitializer.StencilTargetStoreAction == ERenderTargetStoreAction::ENoAction,
					TEXT("No stencil target set, yet PSO wants to store into it!"));
*/
			}
		}
		else
		{
			ensureMsgf(!Initializer.bEnableDepthWrite && Initializer.DepthTest == CF_Always, TEXT("No depth render target set, yet PSO wants to use depth operations!"));
			ensureMsgf(PSOInitializer.DepthTargetLoadAction == ERenderTargetLoadAction::ENoAction
				&& PSOInitializer.StencilTargetLoadAction == ERenderTargetLoadAction::ENoAction,
				TEXT("No depth/stencil target set, yet PSO wants to load from it!"));
			ensureMsgf(PSOInitializer.DepthTargetStoreAction == ERenderTargetStoreAction::ENoAction
				&& PSOInitializer.StencilTargetStoreAction == ERenderTargetStoreAction::ENoAction,
				TEXT("No depth/stencil target set, yet PSO wants to store into it!"));
		}
	}
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
