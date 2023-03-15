// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HitSet.h"
#include "LongJump.h"
#include "TaggedPtr.h"
#include "TaskArray.h"
#include "WriteLog.h"
#include "WriteLogBumpAllocator.h"
#include <functional>

namespace AutoRTFM
{
class FContext;

class FTransaction final
{
public:
    FTransaction(FContext* Context);
    
    bool IsNested() const { return !!Parent; }
    FTransaction* GetParent() const { return Parent; }

    // This should just use type displays or ranges. Maybe ranges could even work out great.
    bool IsNestedWithin(const FTransaction* Other) const
    {
        for (const FTransaction* Current = this; ; Current = Current->Parent)
        {
            if (!Current)
            {
                return false;
            }
            else if (Current == Other)
            {
                return true;
            }
        }
    }

    bool IsFresh() const;
    bool IsDone() const { return bIsDone; }
    void SetIsDone() { bIsDone = true; }
    
    void DeferUntilCommit(std::function<void()>&&);
    void DeferUntilAbort(std::function<void()>&&);

    // Whether this succeeded or not is reflected in Context::GetStatus().
    template<typename TTryFunctor>
    void Try(const TTryFunctor& TryFunctor);

    void AbortAndThrow();
    void AbortWithoutThrowing();
    bool AttemptToCommit();

    // Record that a write is about to occur at the given LogicalAddress of Size bytes.
    void RecordWrite(void* LogicalAddress, size_t Size);
    void RecordWriteMaxPageSized(void* LogicalAddress, size_t Size);

    void DidAllocate(void* LogicalAddress, size_t Size);

private:
    void Undo();
    void AbortNested();
    void AbortOuterNest();
    
    void CommitNested();
    bool AttemptToCommitOuterNest();

    void Reset(); // Frees memory and sets us up for possible retry.
    
    FContext* Context;
    
    // Are we nested? Then this is the parent.
    FTransaction* Parent;

    // Commit tasks run on commit in forward order. Abort tasks run on abort in reverse order.
    TTaskArray<std::function<void()>> CommitTasks;
    TTaskArray<std::function<void()>> AbortTasks;

    bool bIsDone{false};

    FLongJump AbortJump;

    FHitSet HitSet;
    FWriteLog WriteLog;
    FWriteLogBumpAllocator WriteLogBumpAllocator;
};

} // namespace AutoRTFM
