// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SharedPtr.h"

namespace AutoRTFM
{

template<typename T>
class TTaskArray
{
public:
    TTaskArray() = default;

    bool IsEmpty() const { return Latest.IsEmpty() && Stash.IsEmpty(); }

    void Add(T&& value)
    {
        Latest.Push(MoveTemp(value));
    }
    void Add(const T& value)
    {
        Latest.Push(value);
    }

    void AddAll(TTaskArray<T>&& Other)
    {
        Canonicalize();
        for (TSharedPtr<TArray<T>>& StashedVectorBox : Other.Stash)
        {
            Stash.Push(MoveTemp(StashedVectorBox));
        }
        Other.Stash.Empty();
        if (!Other.Latest.IsEmpty())
        {
            Stash.Push(TSharedPtr<TArray<T>>::New(MoveTemp(Other.Latest)));
        }
    }

    void AddAll(const TTaskArray<T>& Other)
    {
        Canonicalize();
        Other.Canonicalize();
        for (const TSharedPtr<TArray<T>>& StashedVectorBox : Other.Stash)
        {
            Stash.Push(StashedVectorBox);
        }
    }

    template<typename TFunc>
    bool ForEachForward(const TFunc& Func)
    {
        for (TSharedPtr<TArray<T>>& StashedVectorBox : Stash)
        {
            for (const T& Entry : *StashedVectorBox)
            {
                if (!Func(Entry))
                {
                    return false;
                }
            }
        }
        for (const T& Entry : Latest)
        {
            if (!Func(Entry))
            {
                return false;
            }
        }
        return true;
    }

    template<typename TFunc>
    bool ForEachBackward(const TFunc& Func)
    {
        for (size_t Index = Latest.Num(); Index--;)
        {
            if (!Func(Latest[Index]))
            {
                return false;
            }
        }
        for (size_t IndexInStash = Stash.Num(); IndexInStash--;)
        {
            const TArray<T>& StashedVector = *Stash[IndexInStash];
            for (size_t Index = StashedVector.Num(); Index--;)
            {
                if (!Func(StashedVector[Index]))
                {
                    return false;
                }
            }
        }
        return true;
    }

    void Reset()
    {
        Latest.Empty();
        Stash.Empty();
    }

private:
    // We don't want to do this too often; currently we just do it where it's asymptitically relevant like AddAll. This doesn't
    // logically change the TaskArray but it changes its internal representation. Hence the use of `mutable` and hence why this
    // method is `const`.
    void Canonicalize() const
    {
        if (!Latest.IsEmpty())
        {
            Stash.Push(TSharedPtr<TArray<T>>::New(MoveTemp(Latest)));
        }
    }
    
    mutable TArray<T> Latest;
    
    mutable TArray<TSharedPtr<TArray<T>>> Stash;
};

} // namespace AutoRTFM
