// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SharedPtr.h"
#include <vector>

namespace AutoRTFM
{

template<typename T>
class TTaskArray
{
public:
    TTaskArray() = default;

    bool IsEmpty() const { return Latest.empty() && Stash.empty(); }

    void Add(T&& value)
    {
        Latest.push_back(std::move(value));
    }
    void Add(const T& value)
    {
        Latest.push_back(value);
    }

    void AddAll(TTaskArray<T>&& Other)
    {
        Canonicalize();
        for (TSharedPtr<std::vector<T>>& StashedVectorBox : Other.Stash)
        {
            Stash.push_back(std::move(StashedVectorBox));
        }
        Other.Stash.clear(); 
        if (!Other.Latest.empty())
        {
            Stash.push_back(TSharedPtr<std::vector<T>>::New(std::move(Other.Latest)));
        }
    }

    void AddAll(const TTaskArray<T>& Other)
    {
        Canonicalize();
        Other.Canonicalize();
        for (const TSharedPtr<std::vector<T>>& StashedVectorBox : Other.Stash)
        {
            Stash.push_back(StashedVectorBox);
        }
    }

    template<typename TFunc>
    bool ForEachForward(const TFunc& Func)
    {
        for (TSharedPtr<std::vector<T>>& StashedVectorBox : Stash)
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
        for (size_t Index = Latest.size(); Index--;)
        {
            if (!Func(Latest[Index]))
            {
                return false;
            }
        }
        for (size_t IndexInStash = Stash.size(); IndexInStash--;)
        {
            const std::vector<T>& StashedVector = *Stash[IndexInStash];
            for (size_t Index = StashedVector.size(); Index--;)
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
        Latest.clear();
        Stash.clear();
    }

private:
    // We don't want to do this too often; currently we just do it where it's asymptitically relevant like AddAll. This doesn't
    // logically change the TaskArray but it changes its internal representation. Hence the use of `mutable` and hence why this
    // method is `const`.
    void Canonicalize() const
    {
        if (!Latest.empty())
        {
            Stash.push_back(TSharedPtr<std::vector<T>>::New(std::move(Latest)));
        }
    }
    
    mutable std::vector<T> Latest;
    
    mutable std::vector<TSharedPtr<std::vector<T>>> Stash;
};

} // namespace AutoRTFM
