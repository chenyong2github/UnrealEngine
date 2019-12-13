// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Misc/StringBuilder.h"

#include "Misc/AssertionMacros.h"
#include "HAL/PlatformMath.h"
#include "HAL/UnrealMemory.h"

//#include "doctest.h"

static inline uint64_t NextPowerOfTwo(uint64_t x)
{
	x -= 1;
	x |= (x >> 1);
	x |= (x >> 2);
	x |= (x >> 4);
	x |= (x >> 8);
	x |= (x >> 16);

	return x + 1;
}

template<typename C>
TStringBuilderImpl<C>::~TStringBuilderImpl()
{
	if (bIsDynamic)
		FreeBuffer(Base, End - Base);
}

template<typename C>
void
TStringBuilderImpl<C>::Extend(int32 ExtraCapacity)
{
	check(bIsExtendable);

	const SIZE_T OldCapacity = End - Base;
	const SIZE_T NewCapacity = NextPowerOfTwo(OldCapacity + ExtraCapacity);

	C* NewBase = (C*)AllocBuffer(NewCapacity);

	SIZE_T Pos = CurPos - Base;
	memcpy(NewBase, Base, Pos * sizeof(C));

	if (bIsDynamic)
	{
		FreeBuffer(Base, OldCapacity);
	}

	Base		= NewBase;
	CurPos		= NewBase + Pos;
	End			= NewBase + NewCapacity;
	bIsDynamic	= true;
}

template<typename C>
void*
TStringBuilderImpl<C>::AllocBuffer(int32 ByteCount)
{
	return FMemory::Malloc(ByteCount * sizeof(C));
}

template<typename C>
void
TStringBuilderImpl<C>::FreeBuffer(void* Buffer, int32 ByteCount)
{
	FMemory::Free(Buffer);
}

template<typename C>
typename TStringBuilderImpl<C>::BuilderType&
TStringBuilderImpl<C>::AppendfImpl(BuilderType& Self, const C* Fmt, ...)
{
	for (;;)
	{
		va_list ArgPack;
		va_start(ArgPack, Fmt);
		const int32 RemainingSize = Self.End - Self.CurPos;
		const int32 Result = TCString<C>::GetVarArgs(Self.CurPos, RemainingSize, Fmt, ArgPack);
		va_end(ArgPack);

		if (Result >= 0 && Result < RemainingSize)
		{
			Self.CurPos += Result;
			return Self;
		}
		else
		{
			// Total size will be rounded up to the next power of two. Start with at least 64.
			Self.Extend(64);
		}
	}
}

// Instantiate templates once

template class TStringBuilderImpl<ANSICHAR>;
template class TStringBuilderImpl<TCHAR>;

#if 0
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

TEST_CASE("Core.StringView")
{
	{
		TAnsiStringBuilder<32> boo;
		CHECK(boo.Len() == 0);
		CHECK(strcmp(boo.ToString(), "") == 0);

		{
			FAnsiStringView view = boo;
			CHECK(boo.Len() == view.Len());
		}

		boo.Append("bababa");

		CHECK(strcmp(boo.ToString(), "bababa") == 0);

		{
			FAnsiStringView view = boo;
			CHECK(boo.Len() == view.Len());
		}
	}

	{
		TWideStringBuilder<32> booboo;
		booboo.Append(TEXT("babababa"));

		{
			FWideStringView view = booboo;
			CHECK(booboo.Len() == view.Len());
		}

		//booboo << TEXT("bobo");
		//booboo << "baba";	// ANSI
		//booboo << 'b';

		char foo[] = "aaaaa";
		char noo[] = "oOoOo";

		booboo << noo;
		booboo << foo;

		FString ouch(TEXT("ouch"));
		booboo << ouch;
	}
}

#endif
#endif
