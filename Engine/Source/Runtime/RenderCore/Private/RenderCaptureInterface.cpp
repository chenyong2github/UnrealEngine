// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderCaptureInterface.h"
#include "RenderingThread.h"

namespace RenderCaptureInterface
{
	static FOnFrameCaptureDelegate GFrameCaptureDelegates;
	static FOnBeginCaptureDelegate GBeginDelegates;
	static FOnEndCaptureDelegate GEndDelegates;

	void RegisterCallbacks(FOnFrameCaptureDelegate InFrameCaptureDelegate)
	{
		check(!GFrameCaptureDelegates.IsBound());
		GFrameCaptureDelegates = InFrameCaptureDelegate;
	}

	void RegisterCallbacks(FOnBeginCaptureDelegate InBeginDelegate, FOnEndCaptureDelegate InEndDelegate)
	{
		check(!GBeginDelegates.IsBound());
		GBeginDelegates = InBeginDelegate;
		check(!GEndDelegates.IsBound());
		GEndDelegates = InEndDelegate;
	}

	void UnregisterCallbacks()
	{
		GFrameCaptureDelegates.Unbind();
		GBeginDelegates.Unbind();
		GEndDelegates.Unbind();
	}


	void FrameCapture()
	{
		if (GFrameCaptureDelegates.IsBound())
		{
			GFrameCaptureDelegates.Execute();
		}
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
		check(!GIsThreadedRendering || !IsInRenderingThread());

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
		check(!GIsThreadedRendering || IsInRenderingThread());

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
				check(!GIsThreadedRendering || IsInRenderingThread());
				
				EndCapture(RHICmdList);
			}
			else
			{
				check(!GIsThreadedRendering || !IsInRenderingThread());

				ENQUEUE_RENDER_COMMAND(CaptureCommand)([](FRHICommandListImmediate& RHICommandListLocal)
				{
					EndCapture(&RHICommandListLocal);
				});
			}
		}
	}
}
