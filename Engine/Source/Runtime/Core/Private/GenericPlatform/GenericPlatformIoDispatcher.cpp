// Copyright Epic Games, Inc. All Rights Reserved.

#include "GenericPlatformIoDispatcher.h"
#include "IO/IoDispatcherFileBackend.h"
#include "Async/AsyncFileHandle.h"
#include "HAL/Event.h"
#include "HAL/PlatformFilemanager.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "HAL/RunnableThread.h"
#include "Misc/ScopeLock.h"

//PRAGMA_DISABLE_OPTIMIZATION

TRACE_DECLARE_INT_COUNTER(IoDispatcherSequentialReads, TEXT("IoDispatcher/SequentialReads"));
TRACE_DECLARE_INT_COUNTER(IoDispatcherForwardSeeks, TEXT("IoDispatcher/ForwardSeeks"));
TRACE_DECLARE_INT_COUNTER(IoDispatcherBackwardSeeks, TEXT("IoDispatcher/BackwardSeeks"));
TRACE_DECLARE_MEMORY_COUNTER(IoDispatcherTotalSeekDistance, TEXT("IoDispatcher/TotalSeekDistance"));
TRACE_DECLARE_INT_COUNTER(IoDispatcherPendingBlocksCount, TEXT("IoDispatcher/PendingBlocksCount"));

FGenericIoDispatcherEventQueue::FGenericIoDispatcherEventQueue()
	: Event(FPlatformProcess::GetSynchEventFromPool())
{
}

FGenericIoDispatcherEventQueue::~FGenericIoDispatcherEventQueue()
{
	FPlatformProcess::ReturnSynchEventToPool(Event);
}

void FGenericIoDispatcherEventQueue::Notify()
{
	Event->Trigger();
}

void FGenericIoDispatcherEventQueue::Wait()
{
	Event->Wait();
}

FGenericFileIoStoreImpl::FGenericFileIoStoreImpl(FGenericIoDispatcherEventQueue& InEventQueue)
	: EventQueue(InEventQueue)
	, PendingBlockEvent(FPlatformProcess::GetSynchEventFromPool())
{
	Thread = FRunnableThread::Create(this, TEXT("IoService"), 0, TPri_AboveNormal);
}

FGenericFileIoStoreImpl::~FGenericFileIoStoreImpl()
{
	delete Thread;
	FPlatformProcess::ReturnSynchEventToPool(PendingBlockEvent);
}

bool FGenericFileIoStoreImpl::OpenContainer(const TCHAR* ContainerFilePath, uint64& ContainerFileHandle, uint64& ContainerFileSize)
{
	IPlatformFile& Ipf = FPlatformFileManager::Get().GetPlatformFile();
	IFileHandle* FileHandle = Ipf.OpenReadNoBuffering(ContainerFilePath);
	if (!FileHandle)
	{
		return false;
	}
	ContainerFileHandle = reinterpret_cast<UPTRINT>(FileHandle);
	ContainerFileSize = FileHandle->Size();
	return true;
}

void FGenericFileIoStoreImpl::BeginReadsForRequest(FFileIoStoreResolvedRequest& ResolvedRequest)
{
	if (!ResolvedRequest.Request->IoBuffer.DataSize())
	{
		ResolvedRequest.Request->IoBuffer = FIoBuffer(ResolvedRequest.ResolvedSize);
	}
}

void FGenericFileIoStoreImpl::ReadBlockFromFile(FFileIoStoreReadBlock* Block)
{
	{
		FScopeLock Lock(&PendingBlocksCritical);
		if (!PendingBlocksHead)
		{
			PendingBlocksHead = PendingBlocksTail = Block;
		}
		else
		{
			PendingBlocksTail->Next = Block;
			PendingBlocksTail = Block;
		}
		Block->Next = nullptr;
		TRACE_COUNTER_INCREMENT(IoDispatcherPendingBlocksCount);
	}
	PendingBlockEvent->Trigger();
}

void FGenericFileIoStoreImpl::EndReadsForRequest()
{

}

FFileIoStoreReadBlock* FGenericFileIoStoreImpl::GetNextCompletedBlock()
{
	FScopeLock _(&CompletedBlocksCritical);
	FFileIoStoreReadBlock* CompletedBlock = CompletedBlocksHead;
	if (!CompletedBlocksHead)
	{
		return nullptr;
	}
	CompletedBlocksHead = CompletedBlocksHead->Next;
	if (!CompletedBlocksHead)
	{
		CompletedBlocksTail = nullptr;
	}
	return CompletedBlock;
}

bool FGenericFileIoStoreImpl::Init()
{
	return true;
}

void FGenericFileIoStoreImpl::Stop()
{
	bStopRequested = true;
	PendingBlockEvent->Trigger();
}

uint32 FGenericFileIoStoreImpl::Run()
{
	FFileIoStoreReadBlock* ScheduledBlocksHead = nullptr;
	FFileIoStoreReadBlock* ScheduledBlocksTail = nullptr;

	while (!bStopRequested)
	{
		while (!bStopRequested)
		{
			{
				FScopeLock PendingBlocksLock(&PendingBlocksCritical);
				if (PendingBlocksHead)
				{
					if (!ScheduledBlocksTail)
					{
						ScheduledBlocksHead = PendingBlocksHead;
						ScheduledBlocksTail = PendingBlocksTail;
					}
					else
					{
						ScheduledBlocksTail->Next = PendingBlocksHead;
						ScheduledBlocksTail = PendingBlocksTail;
					}
					PendingBlocksHead = PendingBlocksTail = nullptr;
				}
			}

			if (!ScheduledBlocksHead)
			{
				break;
			}

			FFileIoStoreReadBlock* BlockToRead = ScheduledBlocksHead;
			ScheduledBlocksHead = ScheduledBlocksHead->Next;
			if (!ScheduledBlocksHead)
			{
				ScheduledBlocksTail = nullptr;
			}
			
			if (!BlockToRead->Buffer.DataSize())
			{
				BlockToRead->Buffer = FIoBuffer(BlockToRead->Size);
			}

			/*FString ScopeNameString;
			ScopeNameString.Appendf(TEXT("ReadBlockFromFile: %lld (%lld)"), BlockToRead->Key.BlockIndex, BlockToRead->Size);
			TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*ScopeNameString);
			*/
			TRACE_CPUPROFILER_EVENT_SCOPE(ReadBlockFromFile);
			IFileHandle* FileHandle = reinterpret_cast<IFileHandle*>(static_cast<UPTRINT>(BlockToRead->Key.FileHandle));
			if (FileHandle->Tell() != BlockToRead->Offset)
			{
				if (uint64(FileHandle->Tell()) > BlockToRead->Offset)
				{
					TRACE_COUNTER_INCREMENT(IoDispatcherBackwardSeeks);
				}
				else
				{
					TRACE_COUNTER_INCREMENT(IoDispatcherForwardSeeks);
				}
				TRACE_COUNTER_ADD(IoDispatcherTotalSeekDistance, FMath::Abs(FileHandle->Tell() - int64(BlockToRead->Offset)));
				FileHandle->Seek(BlockToRead->Offset);
			}
			else
			{
				TRACE_COUNTER_INCREMENT(IoDispatcherSequentialReads);
			}
			FileHandle->Read(BlockToRead->Buffer.Data(), BlockToRead->Size);
			TRACE_COUNTER_DECREMENT(IoDispatcherPendingBlocksCount);
			{
				FScopeLock _(&CompletedBlocksCritical);
				if (!CompletedBlocksHead)
				{
					CompletedBlocksHead = CompletedBlocksTail = BlockToRead;
				}
				else
				{
					CompletedBlocksTail->Next = BlockToRead;
					CompletedBlocksTail = BlockToRead;
				}
				BlockToRead->Next = nullptr;
			}
			EventQueue.Notify();
		}
		PendingBlockEvent->Wait();
	}
	return 0;
}
