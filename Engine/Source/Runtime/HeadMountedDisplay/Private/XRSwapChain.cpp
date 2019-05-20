// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "XRSwapChain.h"
#include "XRThreadUtils.h"
#include "CoreGlobals.h"

#ifdef _DEBUG
#include "CoreMinimal.h"
#include "HAL/RunnableThread.h"
#include "RHI.h"
#endif // _DEBUG

//-------------------------------------------------------------------------------------------------
// FTextureSetProxy
//-------------------------------------------------------------------------------------------------


FORCEINLINE void CheckInRenderThread()
{
#if DO_CHECK
	check(IsInRenderingThread());
#endif
}

bool InRHIOrValidThread()
{
	if (GRenderingThread && !GIsRenderingThreadSuspended.Load(EMemoryOrder::Relaxed))
	{
		if (GRHIThreadId)
		{
			if (FPlatformTLS::GetCurrentThreadId() == GRHIThreadId)
			{
				return true;
			}

			if (FPlatformTLS::GetCurrentThreadId() == GRenderingThread->GetThreadID())
			{
				return GetImmediateCommandList_ForRenderCommand().Bypass();
			}

			return false;
		}
		else
		{
			return FPlatformTLS::GetCurrentThreadId() == GRenderingThread->GetThreadID();
		}
	}
	else
	{
		return IsInGameThread();
	}
}


FORCEINLINE void CheckInRHIThread()
{
#if DO_CHECK
	check(InRHIOrValidThread());
#endif
}

FXRSwapChain::FXRSwapChain(FTextureRHIParamRef InRHITexture, const TArray<FTextureRHIRef>& InRHITextureSwapChain)
	: RHITexture(InRHITexture)
	, RHITextureSwapChain(InRHITextureSwapChain)
	, SwapChainIndex_RHIThread(0)
{
}


FXRSwapChain::~FXRSwapChain()
{
	// @todo: The assertion below fires on exit (even though we're on a valid thread) I'm guessing because everything's getting torn 
	// down and our thread checking isn't valid. Not sure whether to remove the assert here, or figure out a more correct way to 
	// perform the check.
//	CheckInRHIThread();

	ExecuteOnRHIThread([this]()
	{
		ReleaseResources_RHIThread();
	});
}


void FXRSwapChain::GenerateMips_RenderThread(FRHICommandListImmediate& RHICmdList)
{
	CheckInRenderThread();

	if (RHITexture->GetNumMips() > 1 && RHITexture->GetTextureCube() == nullptr)
	{
#if PLATFORM_WINDOWS
		RHICmdList.GenerateMips(RHITexture);
#endif
	}
}


void FXRSwapChain::IncrementSwapChainIndex_RHIThread()
{
	CheckInRHIThread();

	SwapChainIndex_RHIThread = (SwapChainIndex_RHIThread + 1) % GetSwapChainLength();
	GDynamicRHI->RHIAliasTextureResources(RHITexture, RHITextureSwapChain[SwapChainIndex_RHIThread]);
}


void FXRSwapChain::ReleaseResources_RHIThread()
{
	CheckInRHIThread();

	RHITexture = nullptr;
	RHITextureSwapChain.Empty();
}

