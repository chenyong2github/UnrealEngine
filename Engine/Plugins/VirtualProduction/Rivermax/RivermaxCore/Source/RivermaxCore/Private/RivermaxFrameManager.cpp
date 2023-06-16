// Copyright Epic Games, Inc. All Rights Reserved.

#include "RivermaxFrameManager.h"

#include "IRivermaxCoreModule.h"
#include "IRivermaxManager.h"
#include "RivermaxLog.h"

namespace UE::RivermaxCore::Private
{
	FFrameManager::~FFrameManager()
	{
		Cleanup();
	}

	EFrameMemoryLocation FFrameManager::Initialize(const FFrameManagerSetupArgs& Args)
	{
		RivermaxManager = FModuleManager::GetModuleChecked<IRivermaxCoreModule>(TEXT("RivermaxCore")).GetRivermaxManager();
		check(RivermaxManager);

		OnFrameReadyDelegate = Args.OnFrameReadyDelegate;
		OnPreFrameReadyDelegate = Args.OnPreFrameReadyDelegate;
		OnFreeFrameDelegate = Args.OnFreeFrameDelegate;
		OnCriticalErrorDelegate = Args.OnCriticalErrorDelegate;
		FrameResolution = Args.Resolution;
		TotalFrameCount = Args.NumberOfFrames;
		const uint32 FrameSize = FrameResolution.Y * Args.Stride;
		FOnFrameDataCopiedDelegate OnDataCopiedDelegate = FOnFrameDataCopiedDelegate::CreateRaw(this, &FFrameManager::OnDataCopied);

		if (Args.bTryGPUAllocation)
		{
			FrameAllocator = MakeUnique<FGPUAllocator>(FrameResolution, Args.Stride, OnDataCopiedDelegate);
			if (FrameAllocator->Allocate(TotalFrameCount))
			{
				MemoryLocation = EFrameMemoryLocation::GPU;
			}
		}

		if (MemoryLocation == EFrameMemoryLocation::None)
		{
			FrameAllocator = MakeUnique<FSystemAllocator>(FrameResolution, Args.Stride, OnDataCopiedDelegate);
			if (FrameAllocator->Allocate(TotalFrameCount))
			{
				MemoryLocation = EFrameMemoryLocation::System;
			}
		}

		if (MemoryLocation != EFrameMemoryLocation::None)
		{
			// Create frame state tracking containers
			FreeFrames.Reserve(TotalFrameCount);
			PendingFrames.Reserve(TotalFrameCount);
			ReadyFrames.Reserve(TotalFrameCount);
			for (uint32 Index = 0; Index < TotalFrameCount; ++Index)
			{
				// All frames default to being available
				FreeFrames.Add(Index);
			}
		}

		return MemoryLocation;
	}

	void FFrameManager::Cleanup()
	{
		if (FrameAllocator)
		{
			FrameAllocator->Deallocate();
			FrameAllocator.Reset();
		}
	}

	TSharedPtr<UE::RivermaxCore::Private::FRivermaxOutputFrame> FFrameManager::GetFreeFrame()
	{
		FScopeLock Lock(&ContainersCritSec);
		if (!FreeFrames.IsEmpty())
		{
			return FrameAllocator->GetFrame(FreeFrames[0]);
		}

		return nullptr;
	}

	void FFrameManager::MarkAsPending(const TSharedPtr<FRivermaxOutputFrame>& Frame)
	{
		FScopeLock Lock(&ContainersCritSec);

		ensure(FreeFrames.RemoveSingle(Frame->FrameIndex) >= 1);
		PendingFrames.Add(Frame->FrameIndex);
	}

	TSharedPtr<UE::RivermaxCore::Private::FRivermaxOutputFrame> FFrameManager::GetNextFrame(uint32 NextFrameIdentifier)
	{
		FScopeLock Lock(&ContainersCritSec);
		
		TSharedPtr<FRivermaxOutputFrame> NextFrame = GetPendingFrame(NextFrameIdentifier);
		if (NextFrame)
		{
			// We found a pending / reserved frame matching identifier
			return NextFrame;
		}

		//Otherwise, prepare next free frame
		NextFrame = GetFreeFrame();
		if (NextFrame)
		{
			NextFrame->Reset();
			NextFrame->FrameIdentifier = NextFrameIdentifier;
			MarkAsPending(NextFrame);
		}

		return NextFrame;
	}

	TSharedPtr<FRivermaxOutputFrame> FFrameManager::GetPendingFrame(uint32 FrameIdentifier)
	{
		FScopeLock Lock(&ContainersCritSec);
		if (!PendingFrames.IsEmpty())
		{
			for (const uint32 FrameIndex : PendingFrames)
			{
				TSharedPtr<FRivermaxOutputFrame> Frame = FrameAllocator->GetFrame(FrameIndex);
				if (Frame->FrameIdentifier == FrameIdentifier)
				{
					// We found a reserved / pending frame corresponding to next frame identifier
					return Frame;
				}
			}
		}
		return nullptr;
	}

	TSharedPtr<FRivermaxOutputFrame> FFrameManager::GetReadyFrame()
	{
		FScopeLock Lock(&ContainersCritSec);
		if (!ReadyFrames.IsEmpty())
		{
			const uint32 NextReadyFrame = ReadyFrames[0];
			return FrameAllocator->GetFrame(NextReadyFrame);
		}
	
		return nullptr;
	}

	void FFrameManager::MarkAsSent(const TSharedPtr<FRivermaxOutputFrame>& Frame)
	{
		{
			FScopeLock Lock(&ContainersCritSec);

			Frame->Reset();
			FreeFrames.Add(SendingFrame);

			if (ensure(Frame->FrameIndex == SendingFrame))
			{
				SendingFrame = INDEX_NONE;
			}
		}
		
		OnFreeFrameDelegate.ExecuteIfBound();
	}

	void FFrameManager::MarkAsReady(const TSharedPtr<FRivermaxOutputFrame>& Frame)
	{
		{
			// Make frame available to be sent
			FScopeLock Lock(&ContainersCritSec);
			Frame->ReadyTimestamp = RivermaxManager->GetTime();
			PendingFrames.RemoveSingle(Frame->FrameIndex);
			ReadyFrames.Add(Frame->FrameIndex);
		}

		OnFrameReadyDelegate.ExecuteIfBound();
	}

	void FFrameManager::MarkAsSending(const TSharedPtr<FRivermaxOutputFrame>& Frame)
	{
		// Make frame available to be sent
		FScopeLock Lock(&ContainersCritSec);
		if (ensure(!ReadyFrames.IsEmpty()))
		{
			SendingFrame = ReadyFrames[0];
			ensure(SendingFrame == Frame->FrameIndex);
			ReadyFrames.RemoveAt(0);
		}
	}

	const TSharedPtr<FRivermaxOutputFrame> FFrameManager::GetFrame(int32 Index) const
	{
		return FrameAllocator->GetFrame(Index);
	}

	bool FFrameManager::SetFrameData(const FRivermaxOutputVideoFrameInfo& NewFrameInfo)
	{
		bool bSuccess = false;
		if (TSharedPtr<FRivermaxOutputFrame> NextFrame = GetNextFrame(NewFrameInfo.FrameIdentifier))
		{
			 bSuccess = FrameAllocator->CopyData(NewFrameInfo, NextFrame);
			 if (!bSuccess)
			 {
				 OnCriticalErrorDelegate.ExecuteIfBound();
			 }
			 
		}
		return bSuccess;
	}

	void FFrameManager::OnDataCopied(const TSharedPtr<FRivermaxOutputFrame>& Frame)
	{
		OnPreFrameReadyDelegate.ExecuteIfBound();

		if (TSharedPtr<FRivermaxOutputFrame> AvailableFrame = GetNextFrame(Frame->FrameIdentifier))
		{
			if (AvailableFrame->IsReadyToBeSent())
			{
				const FString TraceName = FString::Format(TEXT("Rmax::FrameReady {0}"), { AvailableFrame->FrameIndex });
				TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*TraceName);
				TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*FString::Printf(TEXT("MediaCapturePipe: %u"), AvailableFrame->FrameIdentifier));
				
				MarkAsReady(AvailableFrame);
			}
		}
	}
}

