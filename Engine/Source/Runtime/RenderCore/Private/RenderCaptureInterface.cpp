// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderCaptureInterface.h"
#include "IRenderCaptureProvider.h"
#include "RenderingThread.h"

namespace RenderCaptureInterface
{
	FScopedCapture::FScopedCapture(bool bEnable, TCHAR const* InScopeName)
		: bCapture(bEnable && IRenderCaptureProvider::IsAvailable())
		, RHICommandList(nullptr)
	{
		check(!GUseThreadedRendering || !IsInRenderingThread());

		if (bCapture)
		{
			ENQUEUE_RENDER_COMMAND(BeginCaptureCommand)([ScopeName = FString(InScopeName)](FRHICommandListImmediate& RHICommandListLocal)
			{
				IRenderCaptureProvider::Get().BeginCapture(&RHICommandListLocal, ScopeName);
			});
		}
	}

	FScopedCapture::FScopedCapture(bool bEnable, FRHICommandListImmediate* InRHICommandList, TCHAR const* InScopeName)
		: bCapture(bEnable && IRenderCaptureProvider::IsAvailable())
		, RHICommandList(InRHICommandList)
	{
		check(!GUseThreadedRendering || IsInRenderingThread());

		if (bCapture)
		{
			IRenderCaptureProvider::Get().BeginCapture(RHICommandList, FString(InScopeName));
		}
	}

	FScopedCapture::~FScopedCapture()
	{
		if (bCapture)
		{
			if (RHICommandList != nullptr)
			{
				check(!GUseThreadedRendering || IsInRenderingThread());
				
				IRenderCaptureProvider::Get().EndCapture(RHICommandList);
			}
			else
			{
				check(!GUseThreadedRendering || !IsInRenderingThread());

				ENQUEUE_RENDER_COMMAND(EndCaptureCommand)([](FRHICommandListImmediate& RHICommandListLocal)
				{
					IRenderCaptureProvider::Get().EndCapture(&RHICommandListLocal);
				});
			}
		}
 	}
}
