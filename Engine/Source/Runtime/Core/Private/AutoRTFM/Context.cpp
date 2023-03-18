// Copyright Epic Games, Inc. All Rights Reserved.

#if (defined(__AUTORTFM) && __AUTORTFM)
#include "Context.h"
#include "ContextInlines.h"
#include "FunctionMap.h"
#include "GlobalData.h"
#include "ScopedGuard.h"
#include "TransactionInlines.h"
#include <algorithm>
#include <memory>

namespace AutoRTFM
{

thread_local std::unique_ptr<FContext> ContextTls;

void FContext::InitializeGlobalData()
{
}

FContext* FContext::TryGet()
{
    return ContextTls.get();
}

void FContext::Set()
{
    ContextTls.reset(this);
}

FContext* FContext::Get()
{
    FContext* Result = TryGet();

    if (!Result)
    {
        Result = new FContext();
        Result->Set();
    }

    return Result;
}

bool FContext::IsTransactional()
{
    FContext* Context = TryGet();
    if (!Context)
    {
        return false;
    }

    if (Context->GetStatus() == EContextStatus::OnTrack)
    {
        return true;
    }
    else
    {
        ASSERT(Context->GetStatus() == EContextStatus::Idle);
        return false;
    }
}

ETransactionResult FContext::Transact(void (*Function)(void* Arg), void* Arg)
{
    constexpr bool bVerbose = false;
    
    ASSERT(Status == EContextStatus::Idle || Status == EContextStatus::OnTrack);

    void (*ClonedFunction)(void* Arg, FContext* Context) = FunctionMapTryLookup(Function);
    if (!ClonedFunction)
    {
        fprintf(stderr, "Could not find function %p (%s) in AutoRTFM::FContext::Transact.\n", Function, GetFunctionDescription(Function).c_str());
        return ETransactionResult::AbortedByLanguage;
    }
    
    std::unique_ptr<FTransaction> NewTransactionUniquePtr(new FTransaction(this));
    FTransaction* NewTransaction = NewTransactionUniquePtr.get();

    void* TransactStackAddress = &NewTransactionUniquePtr;
    ASSERT(TransactStackAddress > StackBegin);
    ASSERT(TransactStackAddress < StackEnd);
    TScopedGuard<void*> CurrentTransactStackAddressGuard(CurrentTransactStackAddress, TransactStackAddress);

    if (!CurrentTransaction)
    {
        ASSERT(Status == EContextStatus::Idle);
        CurrentTransaction = NewTransaction;
        OuterTransactStackAddress = TransactStackAddress;
        ETransactionResult Result = ETransactionResult::Committed; // Initialize to something to make the compiler happy.
        for (;;)
        {
            Status = EContextStatus::OnTrack;
            ASSERT(CurrentTransaction->IsFresh());
            CurrentTransaction->Try([&] () { ClonedFunction(Arg, this); });
            ASSERT(Status != EContextStatus::Idle);
            if (Status == EContextStatus::OnTrack)
            {
                if (bVerbose)
                {
                    fprintf(GetLogFile(), "About to commit; my state is:\n");
                    DumpState();
                    fprintf(GetLogFile(), "Committing...\n");
                }

                if (CurrentTransaction->AttemptToCommit())
                {
                    Result = ETransactionResult::Committed;
                    break;
                }

                if (bVerbose)
                {
                    fprintf(GetLogFile(), "Commit failed!\n");
                }

                ASSERT(Status != EContextStatus::OnTrack);
                ASSERT(Status != EContextStatus::Idle);
            }
            if (Status == EContextStatus::AbortedByRequest)
            {
                Result = ETransactionResult::AbortedByRequest;
                break;
            }
            if (Status == EContextStatus::AbortedByLanguage)
            {
                Result = ETransactionResult::AbortedByLanguage;
                break;
            }
            ASSERT(Status == EContextStatus::AbortedByLineInitialization
                   || Status == EContextStatus::AbortedByCommitTimeLineCheck
                   || Status == EContextStatus::AbortedByFailedLockAcquisition);

            if (bVerbose)
            {
                fprintf(GetLogFile(), "About to prelock some locks!\n");
            }
            ASSERT(!bLocksToHoldAreHeld);
            
            bLocksToHoldAreHeld = true;
        }

        Reset();
        // No need to SetIsDone or whatever since all of the transactions have now been blown away.
        return Result;
    }
    else
    {
        ASSERT(Status == EContextStatus::OnTrack);
        FTransaction* PreviousTransaction = CurrentTransaction;
        CurrentTransaction = NewTransaction;
        CurrentTransaction->Try([&] () { ClonedFunction(Arg, this); });

        // We just use this bit to help assertions for now (though we could use it more strongly). Because of how we use this right now,
        // it's OK that it's set before we commit but after we abort.
        NewTransaction->SetIsDone();
        
        if (Status == EContextStatus::OnTrack)
        {
            bool bCommitResult = NewTransaction->AttemptToCommit();
            CurrentTransaction = PreviousTransaction;
            ASSERT(bCommitResult);
            ASSERT(Status == EContextStatus::OnTrack);
            return ETransactionResult::Committed;
        }
        
        CurrentTransaction = PreviousTransaction;

        switch (Status)
        {
        case EContextStatus::AbortedByLineInitialization:
            CurrentTransaction->AbortAndThrow();
            AutoRTFM::Unreachable();
        case EContextStatus::AbortedByRequest:
            Status = EContextStatus::OnTrack;
            return ETransactionResult::AbortedByRequest;
        case EContextStatus::AbortedByLanguage:
            Status = EContextStatus::OnTrack;
            return ETransactionResult::AbortedByLanguage;
        default:
            AutoRTFM::Unreachable();
        }
    }
}

void FContext::AbortByRequestAndThrow()
{
    ASSERT(Status == EContextStatus::OnTrack);
    Status = EContextStatus::AbortedByRequest;
    CurrentTransaction->AbortAndThrow();
}

void FContext::AbortByLanguageAndThrow()
{
    constexpr bool bAbortProgram = false;
    ASSERT(Status == EContextStatus::OnTrack);
    if (bAbortProgram)
    {
        fprintf(stderr, "FATAL: Unexpected language abort.\n");
        abort();
    }
    Status = EContextStatus::AbortedByLanguage;
    CurrentTransaction->AbortAndThrow();
}

#if _WIN32
extern "C" __declspec(dllimport) void __stdcall GetCurrentThreadStackLimits(void**, void**);
#endif

FContext::FContext()
{
#if _WIN32
    GetCurrentThreadStackLimits(&StackBegin, &StackEnd);
#elif defined(__APPLE__)         
   StackEnd = pthread_get_stackaddr_np(pthread_self());   
   size_t StackSize = pthread_get_stacksize_np(pthread_self());    
   StackBegin = static_cast<char*>(StackEnd) - StackSize;
#else
    pthread_attr_t Attr;
    pthread_getattr_np(pthread_self(), &Attr);
    size_t StackSize;
    pthread_attr_getstack(&Attr, &StackBegin, &StackSize);
    StackEnd = static_cast<char*>(StackBegin) + StackSize;
#endif
    ASSERT(StackEnd > StackBegin);
}

void FContext::Reset()
{
    OuterTransactStackAddress = nullptr;
    CurrentTransactStackAddress = nullptr;
    CurrentTransaction = nullptr;
    Status = EContextStatus::Idle;
}

void FContext::DumpState() const
{
    fprintf(GetLogFile(), "Context at %p.\n", this);
    fprintf(GetLogFile(), "Transaction stack: %p...%p\n", StackBegin, OuterTransactStackAddress);
}

} // namespace AutoRTFM

#endif // defined(__AUTORTFM) && __AUTORTFM
