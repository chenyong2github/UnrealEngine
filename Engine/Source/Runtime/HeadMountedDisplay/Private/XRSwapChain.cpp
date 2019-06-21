// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "XRSwapChain.h"
#include "RenderingThread.h"
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

FXRSwapChain::FXRSwapChain(FRHITexture* InRHITexture, TArray<FTextureRHIRef>&& InRHITextureSwapChain)
	: RHITexture(InRHITexture)
	, RHITextureSwapChain(InRHITextureSwapChain)
	, SwapChainIndex_RHIThread(0)
{
	RHITexture->SetName(TEXT("XRSwapChainAliasedTexture"));
	for (int ChainElement = 0; ChainElement < RHITextureSwapChain.Num(); ++ChainElement)
	{
		RHITextureSwapChain[ChainElement]->SetName(FName(*FString::Printf(TEXT("XRSwapChainBackingTex%d"), ChainElement)));
	}
}


FXRSwapChain::~FXRSwapChain()
{
	if (IsInGameThread())
	{
		ExecuteOnRenderThread([this]()
		{
			ExecuteOnRHIThread([this]()
			{
				ReleaseResources_RHIThread();
			});
		});
	}
	else
	{
		ExecuteOnRHIThread([this]()
		{
			ReleaseResources_RHIThread();
		});
	}
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

