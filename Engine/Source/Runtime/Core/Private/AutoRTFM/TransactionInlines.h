// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Context.h"
#include "Transaction.h"
#include <cstddef>

namespace AutoRTFM
{

inline void FTransaction::RecordWriteMaxPageSized(void* LogicalAddress, size_t Size)
{
    ASSERT(Size <= UINT16_MAX);

    FMemoryLocation Key(LogicalAddress);
    Key.SetTopTag(static_cast<uint16_t>(Size));

    // If we are recording a stack address that is relative to our current
    // transactions stack location, we do not need to record the data in the
    // write log because if that transaction aborted, that memory will cease to
    // be meaningful anyway!
    // TODO: with more advanced LLVM-skillz we could just detect writes to
    // alloca's in the LLVM code and that should probably catch all or nearly
    // all of the cases this checks would catch, so we could revisit doing this
    // check entirely in future!
    if (Context->IsInnerTransactionStack(LogicalAddress))
    {
        return;
    }

    if (!HitSet.Insert(Key))
    {
        return;
    }

    void* CopyAddress = WriteLogBumpAllocator.Allocate(Size);
    memcpy(CopyAddress, LogicalAddress, Size);

    WriteLog.Push(FWriteLogEntry(LogicalAddress, Size, CopyAddress));
}

inline void FTransaction::RecordWrite(void* LogicalAddress, size_t Size)
{
    uint8_t* Address = reinterpret_cast<uint8_t*>(LogicalAddress);

    size_t I = 0;

    for (; (I + UINT16_MAX) < Size; I += UINT16_MAX)
    {
        RecordWriteMaxPageSized(Address + I, UINT16_MAX);
    }

    // Remainder at the end of the memcpy.
    RecordWriteMaxPageSized(Address + I, Size - I);
}

inline void FTransaction::DidAllocate(void* LogicalAddress, size_t Size)
{
    if (Size <= UINT16_MAX)
    {
        FMemoryLocation Key(LogicalAddress);
        Key.SetTopTag(static_cast<uint16_t>(Size));
        // Otherwise we need to record the write.
        const bool DidInsert = HitSet.Insert(Key);
        ASSERT(DidInsert); 
    }
}

inline void FTransaction::DeferUntilCommit(std::function<void()>&& Callback)
{
    CommitTasks.Add(std::move(Callback));
}

inline void FTransaction::DeferUntilAbort(std::function<void()>&& Callback)
{
    AbortTasks.Add(std::move(Callback));
}

template<typename TTryFunctor>
void FTransaction::Try(const TTryFunctor& TryFunctor)
{
    AbortJump.TryCatch(
        [&] ()
        {
            TryFunctor();
            ASSERT(Context->GetStatus() == EContextStatus::OnTrack);
        },
        [&] ()
        {
            ASSERT(Context->GetStatus() != EContextStatus::Idle);
            ASSERT(Context->GetStatus() != EContextStatus::OnTrack);
        });
}

} // namespace AutoRTFM
