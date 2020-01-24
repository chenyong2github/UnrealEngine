// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RenderCaptureInterface.h"
#include "RenderingThread.h"

namespace RenderCaptureInterface
{
	static FOnBeginCaptureDelegate GBeginDelegates;
	static FOnEndCaptureDelegate GEndDelegates;
	
	void RegisterCallbacks(FOnBeginCaptureDelegate InBeginDelegate, FOnEndCaptureDelegate InEndDelegate)
	{
		check(!GBeginDelegates.IsBound());
		GBeginDelegates = InBeginDelegate;
		check(!GEndDelegates.IsBound());
		GEndDelegates = InEndDelegate;
	}

	void UnregisterCallbacks()
	{
		GBeginDelegates.Unbind();
		GEndDelegates.Unbind();
	}

	void BeginCapture(FRHICommandListImmediate* RHICommandList, TCHAR const* Name)
	{
		if (GBeginDelegates.IsBound())
		{
			GBeginDelegates.Execute(RHICommandList, Name);
		}
	}

	void EndCapture(FRHICommandListImmediate* RHICommandList)
	{
		if (GEndDelegates.IsBound())
		{
			GEndDelegates.Execute(RHICommandList);
		}
	}

	FScopedCapture::FScopedCapture(bool bEnable, TCHAR const* Name)
		: bCapture(bEnable)
		, RHICmdList(nullptr)
	{
		check(!IsInRenderingThread());

		if (bCapture)
		{
			ENQUEUE_RENDER_COMMAND(CaptureCommand)([Name](FRHICommandListImmediate& RHICommandListLocal)
			{
				BeginCapture(&RHICommandListLocal, Name);
			});
		}
	}

	FScopedCapture::FScopedCapture(bool bEnable, FRHICommandListImmediate* RHICommandList, TCHAR const* Name)
		: bCapture(bEnable)
		, RHICmdList(RHICommandList)
	{
		check(IsInRenderingThread());

		if (bCapture)
		{
			BeginCapture(RHICmdList, Name);
		}
	}

	FScopedCapture::~FScopedCapture()
	{
		if (bCapture)
		{
			if (RHICmdList != nullptr)
			{
				check(IsInRenderingThread());
				
				EndCapture(RHICmdList);
			}
			else
			{
				check(!IsInRenderingThread());

				ENQUEUE_RENDER_COMMAND(CaptureCommand)([](FRHICommandListImmediate& RHICommandListLocal)
				{
					EndCapture(&RHICommandListLocal);
				});
			}
		}
	}
}
