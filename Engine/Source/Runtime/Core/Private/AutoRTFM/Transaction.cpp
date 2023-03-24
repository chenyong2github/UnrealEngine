// Copyright Epic Games, Inc. All Rights Reserved.

#if (defined(__AUTORTFM) && __AUTORTFM)
#include "Transaction.h"
#include "TransactionInlines.h"
#include "CallNestInlines.h"
#include "Debug.h"
#include "GlobalData.h"

namespace AutoRTFM
{

FTransaction::FTransaction(FContext* Context)
    : Context(Context)
{
}

bool FTransaction::IsFresh() const
{
    return HitSet.IsEmpty()
        && WriteLog.IsEmpty()
        && CommitTasks.IsEmpty()
        && AbortTasks.IsEmpty()
        && !bIsDone;
}

void FTransaction::AbortWithoutThrowing()
{
    if (FDebug::bVerbose)
    {
        fprintf(GetLogFile(), "Aborting (%s)\n", GetContextStatusName(Context->GetStatus()));
    }
    ASSERT(Context->GetStatus() == EContextStatus::AbortedByFailedLockAcquisition
           || Context->GetStatus() == EContextStatus::AbortedByLanguage
           || Context->GetStatus() == EContextStatus::AbortedByRequest);
    ASSERT(Context->GetCurrentTransaction() == this);
    if (IsNested())
    {
        AbortNested();
    }
    else
    {
        AbortOuterNest();
    }
    Reset();
}

void FTransaction::AbortAndThrow()
{
    AbortWithoutThrowing();
	Context->Throw();
}

bool FTransaction::AttemptToCommit()
{
    ASSERT(Context->GetStatus() == EContextStatus::OnTrack);
    ASSERT(Context->GetCurrentTransaction() == this);
    bool bResult;
    if (IsNested())
    {
        CommitNested();
        bResult = true;
    }
    else
    {
        bResult = AttemptToCommitOuterNest();
    }
    Reset();
    return bResult;
}

void FTransaction::Undo()
{
    for (FWriteLogEntry& Entry : WriteLog)
    {
        // Skip writes to our current transaction nest if we're scoped. We're about to
		// leave so the changes don't matter. 
        if (IsScopedTransaction() && Context->IsInnerTransactionStack(Entry.OriginalAndSize.Get()))
        {
            continue;
        }

        void* const Original = Entry.OriginalAndSize.Get();
        const size_t Size = Entry.OriginalAndSize.GetTopTag();
        void* const Copy = Entry.Copy;
        memcpy(Original, Copy, Size);
    }
}

void FTransaction::AbortNested()
{
    ASSERT(Parent);

    Undo();

    Parent->CommitTasks.AddAll(AbortTasks);
    Parent->AbortTasks.AddAll(MoveTemp(AbortTasks));
}

void FTransaction::AbortOuterNest()
{
    Undo();

    AbortTasks.ForEachBackward([] (const TFunction<void()>& Task) -> bool { Task(); return true; });

    switch (Context->GetStatus())
    {
    case EContextStatus::AbortedByFailedLockAcquisition:
        break;
    case EContextStatus::AbortedByRequest:
    case EContextStatus::AbortedByLanguage:
        break;
    default:
        ASSERT(!"Should not be reached");
        break;
    }
}

void FTransaction::CommitNested()
{
    ASSERT(Parent);

    // We need to pass our write log to our parent transaction, but with care!
    // We need to discard any writes to locations within the stack of our
    // current transaction, which could be placed there if a child of the
    // current transaction had written to stack local memory in the parent.

    for (FWriteLogEntry& Write : WriteLog)
    {
        // Skip writes that are into our current transactions stack.
        if (IsScopedTransaction() && Context->IsInnerTransactionStack(Write.OriginalAndSize.Get()))
        {
            continue;
        }

        Parent->WriteLog.Push(Write);
        Parent->HitSet.Insert(Write.OriginalAndSize);
    }

    Parent->WriteLogBumpAllocator.Merge(MoveTemp(WriteLogBumpAllocator));

    Parent->CommitTasks.AddAll(MoveTemp(CommitTasks));
    Parent->AbortTasks.AddAll(MoveTemp(AbortTasks));
}

bool FTransaction::AttemptToCommitOuterNest()
{
    ASSERT(!Parent);

    if (FDebug::bVerbose)
    {
        fprintf(GetLogFile(), "About to run commit tasks!\n");
        Context->DumpState();
        fprintf(GetLogFile(), "Running commit tasks...\n");
    }

    CommitTasks.ForEachForward([] (const TFunction<void()>& Task) -> bool { Task(); return true; });

    return true;
}

void FTransaction::Reset()
{
    CommitTasks.Reset();
    AbortTasks.Reset();
	HitSet.Reset();
	WriteLog.Reset();
	WriteLogBumpAllocator.Reset();
}

} // namespace AutoRTFM

#endif // defined(__AUTORTFM) && __AUTORTFM
