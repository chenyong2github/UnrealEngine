// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <stdint.h>

#include "Utils.h"

namespace AutoRTFM
{
	class FWriteLogBumpAllocator final
	{
	public:
		FWriteLogBumpAllocator()
		{
			Reset();
		}

		FWriteLogBumpAllocator(const FWriteLogBumpAllocator&) = delete;

		~FWriteLogBumpAllocator()
		{
			Reset();
		}

		void* Allocate(size_t Bytes)
		{
			ASSERT(Bytes <= FPage::MaxSize);

			if (nullptr == Start)
			{
				Start = new FPage();
				Current = Start;
			}

			if (Bytes <= (FPage::MaxSize - Current->Size))
			{
				void* Result = &Current->Bytes[Current->Size];
				Current->Size += Bytes;
				return Result;
			}

			Current->Next = new FPage();
			Current = Current->Next;

			return Allocate(Bytes);
		}

		void Reset()
		{
			FPage* Page = Start;

			while (nullptr != Page)
			{
				FPage* const Next = Page->Next;
				delete Page;
				Page = Next;
			}

			Start = nullptr;
			Current = nullptr;
		}

		void Merge(FWriteLogBumpAllocator&& Other)
		{
			if (nullptr != Start)
			{
				Current->Next = Other.Start;
			}
			else
			{
				Start = Other.Start;
				Current = Start;
			}

			Other.Start = nullptr;
			Other.Current = nullptr;
		}
	private:

		struct FPage final
		{
			// TODO: This is because we used tagged pointers elsewhere and so
			// the max size of an allocation we will track in the bump allocator
			// is 16 bits.
			constexpr static size_t MaxSize = UINT16_MAX;

			uint8_t Bytes[MaxSize];

			FPage* Next = nullptr;
			size_t Size = 0;
		};

		FPage* Start = nullptr;
		FPage* Current = nullptr;
	};
}
