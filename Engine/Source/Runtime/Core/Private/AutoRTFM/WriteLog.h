// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <stdint.h>

#include "Utils.h"

namespace AutoRTFM
{
	using FMemoryLocation = FHitSet::Key;

    struct FWriteLogEntry final
    {
        FMemoryLocation OriginalAndSize;
        void* Copy;

        FWriteLogEntry() = default;
        FWriteLogEntry(FWriteLogEntry&) = default;
        FWriteLogEntry& operator=(FWriteLogEntry&) = default;

        explicit FWriteLogEntry(void* Original, size_t Size, void* Copy) :
            OriginalAndSize(Original), Copy(Copy)
        {
            OriginalAndSize.SetTopTag(static_cast<uint16_t>(Size));
        }
    };

    class FWriteLog final
    {
        struct FWriteLogEntryBucket final
        {
            static constexpr uint32_t BucketSize = 1024;

            FWriteLogEntryBucket() = default;

            FWriteLogEntry Entries[BucketSize];
            size_t Size = 0;
            FWriteLogEntryBucket* Next = nullptr;
        };

    public:
        void Push(FWriteLogEntry Entry)
        {
            if (nullptr == Start)
            {
                Start = new FWriteLogEntryBucket();
                Current = Start;
            }

            if (FWriteLogEntryBucket::BucketSize == Current->Size)
            {
                Current->Next = new FWriteLogEntryBucket();
                Current = Current->Next;
            }

            Current->Entries[Current->Size++] = Entry;

            TotalSize++;
            return;
        }

        ~FWriteLog()
        {
            Reset();
        }

        struct Iterator final
        {
            explicit Iterator(FWriteLogEntryBucket* const Bucket) : Bucket(Bucket) {}

            FWriteLogEntry& operator*()
            {
                return Bucket->Entries[Offset];
            }

            void operator++()
            {
                Offset++;

                if (Offset == Bucket->Size)
                {
                    Bucket = Bucket->Next;
                    Offset = 0;
                }
            }

            bool operator!=(Iterator& Other) const
            {
                return (Other.Bucket != Bucket) || (Other.Offset != Offset);
            }

        private:
            FWriteLogEntryBucket* Bucket;
            size_t Offset = 0;
        };

        Iterator begin()
        {
            return Iterator(Start);
        }

        Iterator end()
        {
            return Iterator(nullptr);
        }

        void Reset()
        {
            while (nullptr != Start)
            {
                FWriteLogEntryBucket* const Old = Start;
                Start = Start->Next;
                delete Old;
            }

            Current = nullptr;
            TotalSize = 0;
        }

        bool IsEmpty() const
        {
            return 0 == TotalSize;
        }

    private:

        FWriteLogEntryBucket* Start = nullptr;
        FWriteLogEntryBucket* Current = nullptr;
        size_t TotalSize = 0;
    };
}
