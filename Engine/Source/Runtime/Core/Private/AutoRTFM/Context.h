// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AutoRTFM/AutoRTFM.h"
#include "ContextStatus.h"
#include <memory>

namespace AutoRTFM
{

class FLineLock;
class FTransaction;

class FContext
{
public:
    static FContext* TryGet();
    static FContext* Get();
    static bool IsTransactional();
    
    // This is public API
    ETransactionResult Transact(void (*Function)(void* Arg), void* Arg);
    void AbortByRequestAndThrow();

    // Record that a write is about to occur at the given LogicalAddress of Size bytes.
    void RecordWrite(void* LogicalAddress, size_t Size);

    void DidAllocate(void* LogicalAddress, size_t Size);
    void WillDeallocate(void* LogicalAddress, size_t Size);

    // The rest of this is internalish.
    void AbortByLanguageAndThrow();

    FTransaction* GetCurrentTransaction() const { return CurrentTransaction; }
    bool IsTransactionStack(void* LogicalAddress) const { return LogicalAddress >= StackBegin && LogicalAddress < OuterTransactStackAddress; }
    bool IsInnerTransactionStack(void* LogicalAddress) const { return LogicalAddress >= StackBegin && LogicalAddress < CurrentTransactStackAddress; }
    EContextStatus GetStatus() const { return Status; }

    void DumpState() const;

    static void InitializeGlobalData();

private:
    FContext();
    FContext(const FContext&) = delete;

    void Set();
    
    // All of this other stuff ought to be private?
    void Reset();

    void StaticAsserts();
    
    FTransaction* CurrentTransaction{nullptr};
    void* StackBegin{nullptr}; // begin as in the smaller of the two
    void* StackEnd{nullptr};
    void* OuterTransactStackAddress{nullptr};
    void* CurrentTransactStackAddress{nullptr};
    bool bLocksToHoldAreHeld{false};
    EContextStatus Status{EContextStatus::Idle};
};

} // namespace AutoRTFM
