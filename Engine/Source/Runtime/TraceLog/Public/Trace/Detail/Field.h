// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Config.h"

#if UE_TRACE_ENABLED

#include "Atomic.h"
#include "Protocol.h"
#include "Templates/UnrealTemplate.h"
#include "Writer.inl"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
template <typename Type> struct TFieldType;

template <> struct TFieldType<bool>			{ enum { Tid = int(EFieldType::Bool),	Size = sizeof(bool) }; };
template <> struct TFieldType<int8>			{ enum { Tid = int(EFieldType::Int8),	Size = sizeof(int8) }; };
template <> struct TFieldType<int16>		{ enum { Tid = int(EFieldType::Int16),	Size = sizeof(int16) }; };
template <> struct TFieldType<int32>		{ enum { Tid = int(EFieldType::Int32),	Size = sizeof(int32) }; };
template <> struct TFieldType<int64>		{ enum { Tid = int(EFieldType::Int64),	Size = sizeof(int64) }; };
template <> struct TFieldType<uint8>		{ enum { Tid = int(EFieldType::Int8),	Size = sizeof(uint8) }; };
template <> struct TFieldType<uint16>		{ enum { Tid = int(EFieldType::Int16),	Size = sizeof(uint16) }; };
template <> struct TFieldType<uint32>		{ enum { Tid = int(EFieldType::Int32),	Size = sizeof(uint32) }; };
template <> struct TFieldType<uint64>		{ enum { Tid = int(EFieldType::Int64),	Size = sizeof(uint64) }; };
template <> struct TFieldType<float>		{ enum { Tid = int(EFieldType::Float32),Size = sizeof(float) }; };
template <> struct TFieldType<double>		{ enum { Tid = int(EFieldType::Float64),Size = sizeof(double) }; };
template <class T> struct TFieldType<T*>	{ enum { Tid = int(EFieldType::Pointer),Size = sizeof(void*) }; };

template <typename T>
struct TFieldType<T[]>
{
	enum
	{
		Tid  = int(TFieldType<T>::Tid)|int(EFieldType::Array),
		Size = 0,
	};
};

#if 0
template <typename T, int N>
struct TFieldType<T[N]>
{
	enum
	{
		Tid  = int(TFieldType<T>::Tid)|int(EFieldType::Array),
		Size = sizeof(T[N]),
	};
};
#endif // 0



////////////////////////////////////////////////////////////////////////////////
struct FLiteralName
{
	template <uint32 Size>
	explicit FLiteralName(const ANSICHAR (&Name)[Size])
	: Ptr(Name)
	, Length(Size - 1)
	{
		static_assert(Size < 256, "Field name is too large");
	}

	const ANSICHAR* Ptr;
	uint8 Length;
};

////////////////////////////////////////////////////////////////////////////////
struct FFieldDesc
{
	FFieldDesc(const FLiteralName& Name, uint8 Type, uint16 Offset, uint16 Size)
	: Name(Name.Ptr)
	, ValueOffset(Offset)
	, ValueSize(Size)
	, NameSize(Name.Length)
	, TypeInfo(Type)
	{
	}

	const ANSICHAR* Name;
	uint16			ValueOffset;
	uint16			ValueSize;
	uint8			NameSize;
	uint8			TypeInfo;
};



////////////////////////////////////////////////////////////////////////////////
template <int InIndex, int InOffset, typename Type> struct TField;

enum class EIndexPack
{
	FieldCountMask	= 0xff,
	MaybeHasAux		= 0x100,
};

////////////////////////////////////////////////////////////////////////////////
#define TRACE_PRIVATE_FIELD(InIndex, InOffset, Type) \
		enum \
		{ \
			Index	= InIndex, \
			Offset	= InOffset, \
			Tid		= TFieldType<Type>::Tid, \
			Size	= TFieldType<Type>::Size, \
		}; \
		static_assert((Index & int(EIndexPack::FieldCountMask)) <= 127, "Trace events may only have up to a maximum of 127 fields"); \
	private: \
		FFieldDesc FieldDesc; \
	public: \
		TField(const FLiteralName& Name) \
		: FieldDesc(Name, Tid, Offset, Size) \
		{ \
		}

////////////////////////////////////////////////////////////////////////////////
template <int InIndex, int InOffset, typename Type>
struct TField<InIndex, InOffset, Type[]>
{
	TRACE_PRIVATE_FIELD(InIndex|int(EIndexPack::MaybeHasAux), InOffset, Type[]);

	static_assert(sizeof(Private::FWriteBuffer::Overflow) >= sizeof(FAuxHeader), "FWriteBuffer::Overflow is not large enough");

	struct FActionable
	{
		void Write(uint8* __restrict) const {}
	};

	const FActionable operator () (Type const* Data, int32 Count) const
	{
		if (Count > 0)
		{
			int32 Size = (Count * sizeof(Type)) & (FAuxHeader::SizeLimit - 1) & ~(sizeof(Type) - 1);
			Impl((const uint8*)Data, Size);
		}
		return {};
	}

private:
	FORCENOINLINE void Impl(const uint8* Data, int32 Size) const
	{
		using namespace Private;

		// Header
		const int bMaybeHasAux = true;
		FWriteBuffer* Buffer = Writer_GetBuffer();
		Buffer->Cursor += sizeof(FAuxHeader) - bMaybeHasAux;

		auto* Header = (FAuxHeader*)(Buffer->Cursor - sizeof(FAuxHeader));
		Header->Size = Size << 8;
		Header->FieldIndex = uint8(0x80 | (Index & int(EIndexPack::FieldCountMask)));

		bool bCommit = ((uint8*)Header == Buffer->Committed);

		// Array data
		while (true)
		{
			if (Buffer->Cursor >= (uint8*)Buffer)
			{
				Buffer = Writer_NextBuffer(0);
				bCommit = true;
			}

			int32 Remaining = int32((uint8*)Buffer - Buffer->Cursor);
			int32 SegmentSize = (Remaining < Size) ? Remaining : Size;
			memcpy(Buffer->Cursor, Data, SegmentSize);
			Buffer->Cursor += SegmentSize;

			if (bCommit)
			{
				AtomicStoreRelease(&(uint8* volatile&)(Buffer->Committed), Buffer->Cursor);
			}

			Size -= SegmentSize;
			if (Size <= 0)
			{
				break;
			}

			Data += SegmentSize;
		}

		// The auxilary data null terminator.
		Buffer->Cursor[0] = 0;
		Buffer->Cursor++;
	}
};

#if 0
////////////////////////////////////////////////////////////////////////////////
template <int InIndex, int InOffset, typename Type, int Count>
struct TField<InIndex, InOffset, Type[Count]>
{
	TRACE_PRIVATE_FIELD(InIndex, InOffset, Type[Count]);

	struct FActionable
	{
		void Write(uint8* __restrict) const {}
	};

	const FActionable operator () (Type const* Data, int Count) const
	{
	}
};
#endif

////////////////////////////////////////////////////////////////////////////////
template <int InIndex, int InOffset, typename Type>
struct TField
{
	TRACE_PRIVATE_FIELD(InIndex, InOffset, Type);

	struct FActionable
	{
		Type Value;
		void Write(uint8* __restrict Ptr) const
		{
			::memcpy(Ptr + Offset, &Value, Size);
		}
	};

	const FActionable operator () (const Type& __restrict Value) const
	{
		return { Value };
	}
};

#undef TRACE_PRIVATE_FIELD



////////////////////////////////////////////////////////////////////////////////
// Used to terminate the field list and determine an event's size.
enum EventProps {};
template <int InFieldCount, int InSize>
struct TField<InFieldCount, InSize, EventProps>
{
	enum : uint16
	{
		FieldCount	= (InFieldCount & int(EIndexPack::FieldCountMask)),
		Size		= InSize,
		MaybeHasAux	= !!(InFieldCount & int(EIndexPack::MaybeHasAux)),
	};
};

////////////////////////////////////////////////////////////////////////////////
// Access to additional data that can be included along with a logged event.
enum Attachment {};
template <int InOffset>
struct TField<0, InOffset, Attachment>
{
	template <typename LambdaType>
	struct FActionableLambda
	{
		LambdaType& Value;
		void Write(uint8* __restrict Ptr) const
		{
			Value(Ptr + InOffset);
		}
	};

	template <typename LambdaType>
	const FActionableLambda<LambdaType> operator () (LambdaType&& Lambda) const
	{
		return { Forward<LambdaType>(Lambda) };
	}

	struct FActionableMemcpy
	{
		const void* Data;
		uint32 Size;
		void Write(uint8* __restrict Ptr) const
		{
			::memcpy(Ptr + InOffset, Data, Size);
		}
	};

	const FActionableMemcpy operator () (const void* Data, uint32 Size) const
	{
		return { Data, Size };
	}
};

} // namespace Trace

#endif // UE_TRACE_ENABLED
