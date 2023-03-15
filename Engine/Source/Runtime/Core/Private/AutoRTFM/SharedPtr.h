// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Utils.h"

namespace AutoRTFM
{

template<typename T>
class TSharedPtr final
{
public:
    TSharedPtr() = default;

    TSharedPtr(const TSharedPtr<T>& Other)
        : Ptr(Other.Ptr), Count(Other.Count)
    {
        if (nullptr != Ptr)
        {
            ASSERT(nullptr != Count);
            *Count += 1;
        }
        else
        {
            ASSERT(nullptr == Count);
        }
    }

	TSharedPtr(TSharedPtr<T>&& Other)
		: Ptr(Other.Ptr), Count(Other.Count)
	{
        // The move constructor **must** behave like the copy constructor,
        // otherwise MSVC will miscompile our code and free still referenced
        // data (it seems to move the data, but then _somehow_ keep the original
        // data around which it then calls the destructor for!). This is cheap
        // anyway since our data has already been allocated, and the destructor
        // for the moved data will just decrement the already allocated count.
        if (nullptr != Ptr)
        {
            ASSERT(nullptr != Count);
            *Count += 1;
        }
        else
        {
            ASSERT(nullptr == Count);
        }
	}
    
    ~TSharedPtr()
    {
        if (Ptr)
        {
            ASSERT(nullptr != Count);

            *Count -= 1;

            if (0 == *Count)
            {
                delete Ptr;
                delete Count;
            }
        }
        else
        {
            ASSERT(nullptr == Count);
        }
    }

    template<typename... TArguments>
    static TSharedPtr New(TArguments&&... Arguments)
    {
        TSharedPtr Result;
        Result.Ptr = new T(std::forward<TArguments>(Arguments)...);
        Result.Count = new unsigned(1);
        return Result;
    }

    TSharedPtr& operator=(const TSharedPtr<T>& Other)
    {
        ASSERT(nullptr == Ptr);
        ASSERT(nullptr == Count);

        Ptr = Other.Ptr;
        Count = Other.Count;
        
        *Count += 1;
    }

	TSharedPtr& operator=(TSharedPtr<T>&& Other) = delete;

    explicit operator bool() const { return !!Ptr; }

    bool operator==(const TSharedPtr<T>& Other) const
    {
        return Ptr == Other.Ptr;
    }

    bool operator!=(const TSharedPtr<T>& Other) const
    {
        return Ptr != Other.Ptr;
    }

    T& operator*() const { return *Ptr; }
    T* operator->() const { return Ptr; }
    
private:
    T* Ptr{nullptr};
    unsigned* Count{nullptr};
};

} // namespace AutoRTFM
