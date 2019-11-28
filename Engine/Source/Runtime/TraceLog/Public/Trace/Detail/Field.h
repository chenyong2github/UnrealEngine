// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Config.h"

#if UE_TRACE_ENABLED

#include "Protocol.h"
#include "Templates/UnrealTemplate.h"

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
template <int InIndex, int InOffset, int InSize>
struct TFieldBase
{
	enum : uint16
	{
		Index	= InIndex,
		Offset	= InOffset,
		Size	= InSize,
	};
};

////////////////////////////////////////////////////////////////////////////////
template <int Index, int Offset, typename Type> struct TField;

////////////////////////////////////////////////////////////////////////////////
// Used to terminate the field list and determine an event's size.
enum EndOfFields {};
template <int InFieldCount, int Size>
struct TField<InFieldCount, Size, EndOfFields>
{
	enum : uint16 { FieldCount = InFieldCount, Value = Size };
	static_assert(FieldCount <= 127, "Trace events may only have up to a maximum of 127 fields");
};

////////////////////////////////////////////////////////////////////////////////
// Access to additional data that can be included along with a logged event.
enum Attachment {};
template <int Offset>
struct TField<0, Offset, Attachment>
{
	template <typename LambdaType>
	struct FActionableLambda
	{
		LambdaType& Value;
		void Write(uint8* __restrict Ptr) const
		{
			Value(Ptr + Offset);
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
			::memcpy(Ptr + Offset, Data, Size);
		}
	};

	const FActionableMemcpy operator () (const void* Data, uint32 Size) const
	{
		return { Data, Size };
	}
};

#if 0
////////////////////////////////////////////////////////////////////////////////
template <typename Type>
struct TField<Type[]>
	: public TFieldBase<sizeof(uint32)>
{
	TField(const ANSICHAR (Name)[]) : TFieldBase<sizeof(uint32)>(Name) {}
};

////////////////////////////////////////////////////////////////////////////////
template <int Offset, typename Type, int Count>
struct TField<Offset, Type[Count]>
	: public TFieldBase<Offset, Type>
{
	TField(const ANSICHAR (Name)[]) : TFieldBase<Offset, Type[Count]>(Name) {}
};
#endif

////////////////////////////////////////////////////////////////////////////////
template <int Index, int Offset, typename Type>
struct TField
	: public TFieldBase<Index, Offset, TFieldType<Type>::Size>
	, public FFieldDesc
{
	TField(const FLiteralName& Name)
	: FFieldDesc(Name, TFieldType<Type>::Tid, Offset, TFieldType<Type>::Size)
	{
	}

	struct FActionable
	{
		Type Value;
		void Write(uint8* __restrict Ptr) const
		{
			::memcpy(Ptr + Offset, &Value, TFieldType<Type>::Size);
		}
	};

	const FActionable operator () (const Type& __restrict Value) const
	{
		return { Value };
	}
};

} // namespace Trace

#endif // UE_TRACE_ENABLED
