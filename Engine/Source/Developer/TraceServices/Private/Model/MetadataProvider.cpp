// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetadataProvider.h"

#include "Common/Utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

namespace TraceServices
{

////////////////////////////////////////////////////////////////////////////////////////////////////
// FMetadataProviderLock
////////////////////////////////////////////////////////////////////////////////////////////////////

thread_local FMetadataProviderLock* GThreadCurrentMetadataProviderLock;
thread_local int32 GThreadCurrentReadMetadataProviderLockCount;
thread_local int32 GThreadCurrentWriteMetadataProviderLockCount;

void FMetadataProviderLock::ReadAccessCheck() const
{
	checkf(GThreadCurrentMetadataProviderLock == this && (GThreadCurrentReadMetadataProviderLockCount > 0 || GThreadCurrentWriteMetadataProviderLockCount > 0),
		TEXT("Trying to READ from metadata provider outside of a READ scope"));
}

void FMetadataProviderLock::WriteAccessCheck() const
{
	checkf(GThreadCurrentMetadataProviderLock == this && GThreadCurrentWriteMetadataProviderLockCount > 0,
		TEXT("Trying to WRITE to metadata provider outside of an EDIT/WRITE scope"));
}

void FMetadataProviderLock::BeginRead()
{
	check(!GThreadCurrentMetadataProviderLock || GThreadCurrentMetadataProviderLock == this);
	checkf(GThreadCurrentWriteMetadataProviderLockCount == 0, TEXT("Trying to lock metadata provider for READ while holding EDIT/WRITE access"));
	if (GThreadCurrentReadMetadataProviderLockCount++ == 0)
	{
		GThreadCurrentMetadataProviderLock = this;
		RWLock.ReadLock();
	}
}

void FMetadataProviderLock::EndRead()
{
	check(GThreadCurrentReadMetadataProviderLockCount > 0);
	if (--GThreadCurrentReadMetadataProviderLockCount == 0)
	{
		RWLock.ReadUnlock();
		GThreadCurrentMetadataProviderLock = nullptr;
	}
}

void FMetadataProviderLock::BeginWrite()
{
	check(!GThreadCurrentMetadataProviderLock || GThreadCurrentMetadataProviderLock == this);
	checkf(GThreadCurrentReadMetadataProviderLockCount == 0, TEXT("Trying to lock metadata provider for EDIT/WRITE while holding READ access"));
	if (GThreadCurrentWriteMetadataProviderLockCount++ == 0)
	{
		GThreadCurrentMetadataProviderLock = this;
		RWLock.WriteLock();
	}
}

void FMetadataProviderLock::EndWrite()
{
	check(GThreadCurrentWriteMetadataProviderLockCount > 0);
	if (--GThreadCurrentWriteMetadataProviderLockCount == 0)
	{
		RWLock.WriteUnlock();
		GThreadCurrentMetadataProviderLock = nullptr;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FMetadataProvider
////////////////////////////////////////////////////////////////////////////////////////////////////

FMetadataProvider::FMetadataProvider(IAnalysisSession& InSession)
	: Session(InSession)
	, RegisteredTypes(Session.GetLinearAllocator(), 16)
	, MetadataStore(Session.GetLinearAllocator(), 1024)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FMetadataProvider::~FMetadataProvider()
{
	for (const auto& KV : Threads)
	{
		for (const FMetadataStackEntry& StackEntry : KV.Value->CurrentStack)
		{
			if (StackEntry.StoreIndex == InvalidMetadataStoreIndex)
			{
				InternalFreeStoreEntry(StackEntry.StoreEntry);
			}
		}
		delete KV.Value;
	}

	if (MetadataStore.Num() > 0)
	{
		for (const FMetadataStoreEntry& StoreEntry : MetadataStore)
		{
			InternalFreeStoreEntry(StoreEntry);
		}
	}

	check(AllocationCount == 0);
	check(TotalAllocatedMemory == 0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

uint16 FMetadataProvider::RegisterMetadataType(const TCHAR* InName, const FMetadataSchema& InSchema)
{
	Lock.WriteAccessCheck();

	check(RegisteredTypes.Num() <= MaxMetadataTypeId);
	const uint16 Type = static_cast<uint16>(RegisteredTypes.Num());
	RegisteredTypes.EmplaceBack(InSchema);
	RegisteredTypesMap.Add(InName, Type);

	return Type;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

uint16 FMetadataProvider::GetRegisteredMetadataType(FName InName) const
{
	Lock.ReadAccessCheck();

	const uint16* TypePtr = RegisteredTypesMap.Find(InName);
	return TypePtr ? *TypePtr : InvalidMetadataType;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FName FMetadataProvider::GetRegisteredMetadataName(uint16 InType) const
{
	Lock.ReadAccessCheck();

	const FName* Name = RegisteredTypesMap.FindKey(InType);
	return Name ? *Name : FName();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FMetadataSchema* FMetadataProvider::GetRegisteredMetadataSchema(uint16 InType) const
{
	Lock.ReadAccessCheck();

	const int32 Index = (int32)InType;
	return Index < RegisteredTypes.Num() ? &RegisteredTypes[Index] : nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FMetadataProvider::FMetadataThread::FMetadataThread(uint32 InThreadId, ILinearAllocator& InAllocator)
	: ThreadId(InThreadId)
	, CurrentStack()
	, Metadata(InAllocator, 1024)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FMetadataProvider::FMetadataThread& FMetadataProvider::GetOrAddThread(uint32 InThreadId)
{
	FMetadataThread** MetadataThreadPtr = Threads.Find(InThreadId);
	if (MetadataThreadPtr)
	{
		return **MetadataThreadPtr;
	}
	FMetadataThread* MetadataThread = new FMetadataThread(InThreadId, Session.GetLinearAllocator());
	Threads.Add(InThreadId, MetadataThread);
	return *MetadataThread;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FMetadataProvider::FMetadataThread* FMetadataProvider::GetThread(uint32 InThreadId)
{
	FMetadataThread** MetadataThreadPtr = Threads.Find(InThreadId);
	return (MetadataThreadPtr) ? *MetadataThreadPtr : nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const FMetadataProvider::FMetadataThread* FMetadataProvider::GetThread(uint32 InThreadId) const
{
	FMetadataThread* const* MetadataThreadPtr = Threads.Find(InThreadId);
	return (MetadataThreadPtr) ? *MetadataThreadPtr : nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMetadataProvider::InternalAllocStoreEntry(FMetadataStoreEntry& InStoreEntry, uint16 InType, const void* InData, uint32 InSize)
{
	if (InSize > MaxInlinedMetadataSize)
	{
		InStoreEntry.Ptr = FMemory::Malloc(InSize);
		++AllocationEventCount;
		++AllocationCount;
		TotalAllocatedMemory += InSize;
		FMemory::Memcpy(InStoreEntry.Ptr, InData, InSize);
	}
	else
	{
		FMemory::Memcpy(InStoreEntry.Value, InData, InSize);
	}
	InStoreEntry.Size = static_cast<uint16>(InSize);
	InStoreEntry.Type = InType;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMetadataProvider::InternalFreeStoreEntry(const FMetadataStoreEntry& InStoreEntry)
{
	if (InStoreEntry.Size > MaxInlinedMetadataSize)
	{
		FMemory::Free(InStoreEntry.Ptr);
		check(AllocationCount > 0);
		--AllocationCount;
		check(TotalAllocatedMemory >= InStoreEntry.Size);
		TotalAllocatedMemory -= InStoreEntry.Size;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMetadataProvider::InternalPushStackEntry(FMetadataThread& InMetadataThread, uint16 InType, const void* InData, uint32 InSize)
{
	FMetadataStackEntry StackEntry;
	InternalAllocStoreEntry(StackEntry.StoreEntry, InType, InData, InSize);
	StackEntry.StoreIndex = InvalidMetadataStoreIndex;
	StackEntry.PinnedId = InvalidMetadataId;
	InMetadataThread.CurrentStack.Push(StackEntry);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMetadataProvider::InternalPopStackEntry(FMetadataThread& InMetadataThread)
{
	const FMetadataStackEntry& Top = InMetadataThread.CurrentStack.Top();
	if (Top.StoreIndex == InvalidMetadataStoreIndex)
	{
		InternalFreeStoreEntry(Top.StoreEntry);
	}
	InMetadataThread.CurrentStack.Pop();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMetadataProvider::InternalClearStack(FMetadataThread& InMetadataThread)
{
	while (InMetadataThread.CurrentStack.Num() > 0)
	{
		InternalPopStackEntry(InMetadataThread);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMetadataProvider::PushScopedMetadata(uint32 InThreadId, uint16 InType, const void* InData, uint32 InSize)
{
	Lock.WriteAccessCheck();
	++EventCount;

	check(InType < RegisteredTypes.Num());
	check(InData != nullptr && InSize > 0);

	if (InSize > MaxMetadataSize)
	{
		UE_LOG(LogTraceServices, Error, TEXT("[Meta] Cannot push metadata (thread %u, type %u, size %u). Data size is too large."), InThreadId, InType, InSize);
		return;
	}

	FMetadataThread& MetadataThread = GetOrAddThread(InThreadId);

	if (MetadataThread.CurrentStack.Num() == MaxMetadataStackSize)
	{
		UE_LOG(LogTraceServices, Error, TEXT("[Meta] Cannot push metadata (thread %u, type %u). Stack size is too large."), InThreadId, InType);
		return;
	}

	InternalPushStackEntry(MetadataThread, InType, InData, InSize);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMetadataProvider::PopScopedMetadata(uint32 InThreadId, uint16 InType)
{
	Lock.WriteAccessCheck();
	++EventCount;

	FMetadataThread& MetadataThread = GetOrAddThread(InThreadId);

	if (MetadataThread.CurrentStack.Num() == 0)
	{
		UE_LOG(LogTraceServices, Error, TEXT("[Meta] Cannot pop metadata (thread %u, type %u). Stack is empty."), InThreadId, uint32(InType));
		return;
	}

	const FMetadataStackEntry& Top = MetadataThread.CurrentStack.Top();

	if (Top.StoreEntry.Type != InType)
	{
		UE_LOG(LogTraceServices, Error, TEXT("[Meta] Cannot pop metadata (thread %u, type %u). Type mismatch."), InThreadId, uint32(InType));
		return;
	}

	InternalPopStackEntry(MetadataThread);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

uint32 FMetadataProvider::PinAndGetId(uint32 InThreadId)
{
	Lock.WriteAccessCheck();

	FMetadataThread* MetadataThread = GetThread(InThreadId);

	if (!MetadataThread || MetadataThread->CurrentStack.Num() == 0)
	{
		return InvalidMetadataId;
	}

	TArray<FMetadataStackEntry>& CurrentStack = MetadataThread->CurrentStack;
	TPagedArray<FMetadataEntry>& Metadata = MetadataThread->Metadata;

	const uint32 LastPinnedId = CurrentStack.Top().PinnedId;
	if (LastPinnedId != InvalidMetadataId)
	{
		return LastPinnedId;
	}

	const int32 StackSize = CurrentStack.Num();

	// Store all entries of the current stack.
	for (int32 StackIndex = StackSize - 1; StackIndex >= 0; --StackIndex)
	{
		FMetadataStackEntry& StackEntry = CurrentStack[StackIndex];
		if (StackEntry.PinnedId != InvalidMetadataId)
		{
			break;
		}
		if (StackEntry.StoreIndex == InvalidMetadataStoreIndex)
		{
			StackEntry.StoreIndex = static_cast<uint32>(MetadataStore.Num());
			MetadataStore.EmplaceBack(StackEntry.StoreEntry);
		}
	}

	int32 FirstUnpinnedStackIndex = StackSize - 1;
	for (int32 StackIndex = StackSize - 2; StackIndex >= 0; --StackIndex)
	{
		const FMetadataStackEntry& StackEntry = CurrentStack[StackIndex];
		if (StackEntry.PinnedId != InvalidMetadataId)
		{
			break;
		}
		FirstUnpinnedStackIndex = StackIndex;
	}
	if (FirstUnpinnedStackIndex > 0)
	{
		const FMetadataStackEntry& LastPinnedStackEntry = CurrentStack[FirstUnpinnedStackIndex - 1];
		if (LastPinnedStackEntry.PinnedId == Metadata.Num() - 1)
		{
			// Reuse the partial pinned metadata stack.
			// Only pin additional metadata entries.
			for (int32 StackIndex = FirstUnpinnedStackIndex; StackIndex < StackSize; ++StackIndex)
			{
				FMetadataStackEntry& StackEntry = CurrentStack[StackIndex];
				StackEntry.PinnedId = static_cast<uint32>(Metadata.Num());
				FMetadataEntry& Entry = Metadata.PushBack();
				Entry.StoreIndex = StackEntry.StoreIndex;
				Entry.Type = StackEntry.StoreEntry.Type;
				Entry.StackSize = static_cast<uint16>(StackIndex + 1);
			}

			return CurrentStack.Top().PinnedId;
		}
	}

	// Pin all metadata entries in the current stack.
	for (int32 StackIndex = StackSize - 1; StackIndex >= 0; --StackIndex)
	{
		FMetadataStackEntry& StackEntry = CurrentStack[StackIndex];
		StackEntry.PinnedId = static_cast<uint32>(Metadata.Num());
		FMetadataEntry& Entry = Metadata.PushBack();
		Entry.StoreIndex = StackEntry.StoreIndex;
		Entry.Type = StackEntry.StoreEntry.Type;
		Entry.StackSize = static_cast<uint16>(StackIndex + 1);
	}

	return CurrentStack.Top().PinnedId;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

uint32 FMetadataProvider::GetMetadataStackSize(uint32 InThreadId, uint32 InMetadataId) const
{
	Lock.ReadAccessCheck();

	const FMetadataThread* MetadataThread = GetThread(InThreadId);

	if (!MetadataThread || InMetadataId >= MetadataThread->Metadata.Num())
	{
		UE_LOG(LogTraceServices, Warning, TEXT("[Meta] Invalid metadata id (thread %u, id %u)."), InThreadId, InMetadataId);
		return 0;
	}

	auto Iterator = MetadataThread->Metadata.GetIteratorFromItem(InMetadataId);
	const FMetadataEntry* Entry = Iterator.GetCurrentItem();
	return Entry->StackSize;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FMetadataProvider::GetMetadata(uint32 InThreadId, uint32 InMetadataId, uint32 InStackDepth, uint16& OutType, const void*& OutData, uint32& OutSize) const
{
	Lock.ReadAccessCheck();

	const FMetadataThread* MetadataThread = GetThread(InThreadId);

	if (!MetadataThread || InMetadataId >= MetadataThread->Metadata.Num())
	{
		UE_LOG(LogTraceServices, Warning, TEXT("[Meta] Invalid metadata id (thread %u, id %u)."), InThreadId, InMetadataId);
		OutType = InvalidMetadataType;
		OutData = nullptr;
		OutSize = 0;
		return false;
	}

	auto Iterator = MetadataThread->Metadata.GetIteratorFromItem(InMetadataId);
	const FMetadataEntry* Entry = Iterator.GetCurrentItem();
	if (InStackDepth >= Entry->StackSize)
	{
		UE_LOG(LogTraceServices, Warning, TEXT("[Meta] Invalid metadata stack detph (thread %u, id %u, depth %u)."), InThreadId, InMetadataId, InStackDepth);
		OutType = InvalidMetadataType;
		OutData = nullptr;
		OutSize = 0;
		return false;
	}
	while (Entry->StackSize != InStackDepth + 1)
	{
		Entry = Iterator.PrevItem();
		check(Entry != nullptr);
	}

	check(Entry->StoreIndex < MetadataStore.Num());
	const FMetadataStoreEntry& StoreEntry = *MetadataStore.GetIteratorFromItem(Entry->StoreIndex);
	OutType = StoreEntry.Type;
	OutData = (StoreEntry.Size > MaxInlinedMetadataSize) ? StoreEntry.Ptr : StoreEntry.Value;
	OutSize = StoreEntry.Size;
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMetadataProvider::EnumerateMetadata(uint32 InThreadId, uint32 InMetadataId, TFunctionRef<bool(uint32 StackDepth, uint16 Type, const void* Data, uint32 Size)> Callback) const
{
	Lock.ReadAccessCheck();

	const FMetadataThread* MetadataThread = GetThread(InThreadId);

	if (!MetadataThread || InMetadataId >= MetadataThread->Metadata.Num())
	{
		UE_LOG(LogTraceServices, Warning, TEXT("[Meta] Invalid metadata id (thread %u, id %u)."), InThreadId, InMetadataId);
		return;
	}

	auto Iterator = MetadataThread->Metadata.GetIteratorFromItem(InMetadataId);
	const FMetadataEntry* Entry = Iterator.GetCurrentItem();
	while (true)
	{
		check(Entry->StoreIndex < MetadataStore.Num());
		const FMetadataStoreEntry& StoreEntry = *MetadataStore.GetIteratorFromItem(Entry->StoreIndex);
		const void* Data = (StoreEntry.Size > MaxInlinedMetadataSize) ? StoreEntry.Ptr : StoreEntry.Value;
		if (!Callback(Entry->StackSize - 1, StoreEntry.Type, Data, StoreEntry.Size))
		{
			break;
		}
		if (Entry->StackSize == 1) // last one
		{
			break;
		}
		Entry = Iterator.PrevItem();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FName GetMetadataProviderName()
{
	static FName Name(TEXT("MetadataProvider"));
	return Name;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const IMetadataProvider* ReadMetadataProvider(const IAnalysisSession& Session)
{
	return Session.ReadProvider<IMetadataProvider>(GetMetadataProviderName());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace TraceServices
