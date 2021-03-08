// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderCaptureInterface.h"
#include "IRenderCaptureProvider.h"
#include "RenderingThread.h"
#include "RHI.h"

namespace RenderCaptureInterface
{
	FScopedCapture::FScopedCapture(bool bEnable, TCHAR const* InEventName, TCHAR const* InFileName)
		: bCapture(bEnable && IRenderCaptureProvider::IsAvailable())
		, bEvent(false)
		, RHICommandList(nullptr)
	{
		check(!GUseThreadedRendering || !IsInRenderingThread());

		if (bCapture)
		{
			ENQUEUE_RENDER_COMMAND(BeginCaptureCommand)([this, EventName = FString(InEventName), FileName = FString(InFileName)](FRHICommandListImmediate& RHICommandListLocal)
			{
				IRenderCaptureProvider::Get().BeginCapture(&RHICommandListLocal, IRenderCaptureProvider::ECaptureFlags_Launch, FileName);

				if (!EventName.IsEmpty())
				{
					RHICommandListLocal.PushEvent(*EventName, FColor::White);
					bEvent = true;
				}
			});
		}
	}

	FScopedCapture::FScopedCapture(bool bEnable, FRHICommandListImmediate* InRHICommandList, TCHAR const* InEventName, TCHAR const* InFileName)
		: bCapture(bEnable && IRenderCaptureProvider::IsAvailable())
		, RHICommandList(InRHICommandList)
	{
		check(!GUseThreadedRendering || IsInRenderingThread());

		if (bCapture)
		{
			IRenderCaptureProvider::Get().BeginCapture(RHICommandList, IRenderCaptureProvider::ECaptureFlags_Launch, FString(InFileName));
		
			if (InEventName != nullptr)
			{
				RHICommandList->PushEvent(InEventName, FColor::White);
				bEvent = true;
			}
		}
	}

	FScopedCapture::~FScopedCapture()
	{
		if (bCapture)
		{
			if (RHICommandList != nullptr)
			{
				check(!GUseThreadedRendering || IsInRenderingThread());
				
				if (bEvent)
				{
					RHICommandList->PopEvent();
				}

				IRenderCaptureProvider::Get().EndCapture(RHICommandList);
			}
			else
			{
				check(!GUseThreadedRendering || !IsInRenderingThread());

				ENQUEUE_RENDER_COMMAND(EndCaptureCommand)([bPopEvent = bEvent](FRHICommandListImmediate& RHICommandListLocal)
				{
					if (bPopEvent)
					{
						RHICommandListLocal.PopEvent();
					}

					IRenderCaptureProvider::Get().EndCapture(&RHICommandListLocal);
				});
			}
		}
 	}
}
