// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Context.h"
#include "Transaction.h"

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

inline void FTransaction::RecordWrite(void* LogicalAddress, size_t Size, bool bIsClosed)
{
	// We don't record any writes to unscoped transactions when the write-address
	// is within the nearest closed C++ function call nest. This may seem weird, but we
	// have to prohibit this because we can't track all the comings and goings of
	// local variables and all child C++ scopes. It's easy to get into a situation
	// where a rollback will undo the recorded writes to stack-locals that actually
	// corrupts the undo process itself.
	if (!IsScopedTransaction()
		&& !bIsClosed
		&& Context->IsInnerTransactionStack(LogicalAddress))
	{
		// It is an error to write to stack variables within an open nest
		fprintf(stderr, "FATAL: Writing to local stack memory from an unscoped transaction is not allowed.\n");
		abort();
	}

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

inline void FTransaction::DeferUntilCommit(TFunction<void()>&& Callback)
{
    CommitTasks.Add(MoveTemp(Callback));
}

inline void FTransaction::DeferUntilAbort(TFunction<void()>&& Callback)
{
    AbortTasks.Add(MoveTemp(Callback));
}

} // namespace AutoRTFM
