// Copyright Epic Games, Inc. All Rights Reserved.

#include "GenericPlatformIoDispatcher.h"
#include "IO/IoDispatcherFileBackend.h"
#include "Async/AsyncFileHandle.h"
#include "HAL/Event.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/ScopeLock.h"

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

void FGenericIoDispatcherEventQueue::ProcessEvents()
{
	Event->Wait();
}

FGenericFileIoStoreImpl::FGenericFileIoStoreImpl(FGenericIoDispatcherEventQueue& InEventQueue)
	: EventQueue(InEventQueue)
{
	
}

bool FGenericFileIoStoreImpl::OpenContainer(const TCHAR* ContainerFilePath, uint64& ContainerFileHandle, uint64& ContainerFileSize)
{
	IPlatformFile& Ipf = FPlatformFileManager::Get().GetPlatformFile();
	{
		TUniquePtr<IFileHandle> FileHandle(Ipf.OpenRead(ContainerFilePath));
		if (!FileHandle)
		{
			return false;
		}
		ContainerFileSize = FileHandle->Size();
	}
	IAsyncReadFileHandle* AsyncFileHandle = Ipf.OpenAsyncRead(ContainerFilePath);
	if (!AsyncFileHandle)
	{
		return false;
	}
	ContainerFileHandle = reinterpret_cast<UPTRINT>(AsyncFileHandle);
	return true;
}

void FGenericFileIoStoreImpl::BeginReadsForRequest(FFileIoStoreResolvedRequest& ResolvedRequest)
{
	if (!ResolvedRequest.Request->IoBuffer.DataSize())
	{
		ResolvedRequest.Request->IoBuffer = FIoBuffer(ResolvedRequest.ResolvedSize);
	}
}

void FGenericFileIoStoreImpl::ReadBlockFromFile(FFileIoStoreReadBlock* Block, uint8* Destination, uint64 FileHandle, uint64 Size, uint64 Offset)
{
	FAsyncFileCallBack ReadCallback = [this, Block](bool bWasCancelled, IAsyncReadRequest* Request)
	{
		check(!bWasCancelled);
		{
			FScopeLock _(&CompletedReadRequestsCritical);
			CompletedReadRequests.AddTail(MakeTuple(Request, Block));
		}
		EventQueue.Notify();
	};

	IAsyncReadFileHandle* AsyncReadFileHandle = reinterpret_cast<IAsyncReadFileHandle*>(static_cast<UPTRINT>(FileHandle));
	AsyncReadFileHandle->ReadRequest(Offset, Size, AIOP_Normal, &ReadCallback, Destination);
}

void FGenericFileIoStoreImpl::EndReadsForRequest()
{

}

FFileIoStoreReadBlock* FGenericFileIoStoreImpl::GetNextCompletedBlock()
{
	IAsyncReadRequest* AsyncReadRequest;
	FFileIoStoreReadBlock* CompletedBlock;
	TDoubleLinkedList<TTuple<IAsyncReadRequest*, FFileIoStoreReadBlock*>>::TDoubleLinkedListNode* CompletedNode;
	{
		FScopeLock _(&CompletedReadRequestsCritical);
		CompletedNode = CompletedReadRequests.GetHead();
		if (!CompletedNode)
		{
			return nullptr;
		}
		TTuple<IAsyncReadRequest*, FFileIoStoreReadBlock*> NodeValue = CompletedNode->GetValue();
		AsyncReadRequest = NodeValue.Get<0>();
		CompletedBlock = NodeValue.Get<1>();
		CompletedReadRequests.RemoveNode(CompletedNode);
	}
	AsyncReadRequest->WaitCompletion();
	delete AsyncReadRequest;
	return CompletedBlock;
}

