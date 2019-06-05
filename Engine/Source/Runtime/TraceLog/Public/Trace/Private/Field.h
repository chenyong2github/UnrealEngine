// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
enum : uint8
{
	/* Category */
	_Field_Integer		= 0000, 
	_Field_Float		= 0100,
	_Field_Array		= 0200, /* flag */

	/* Size */
	_Field_Pow2SizeMask	= 0003,
	_Field_8			= 0000,
	_Field_16			= 0001,
	_Field_32			= 0002,
	_Field_64			= 0003,
#if PLATFORM_64BITS
	_Field_Ptr			= _Field_64,
#else
	_Field_Ptr			= _Field_32,
#endif

	/* unused			= 0004, flag */
	/* unused			= 0070, enum/flags*/
};

////////////////////////////////////////////////////////////////////////////////
enum EFieldType : uint8
{
	Field_Bool		= _Field_Integer | _Field_8,
	Field_Int8		= _Field_Integer | _Field_8,
	Field_Int16		= _Field_Integer | _Field_16,
	Field_Int32		= _Field_Integer | _Field_32,
	Field_Int64		= _Field_Integer | _Field_64,
	Field_Ptr		= _Field_Integer | _Field_Ptr,
	Field_Float		= _Field_Float	 | _Field_32,
	Field_Double	= _Field_Float	 | _Field_64,

	/* Flags */
	Field_Array		= _Field_Array,
};

////////////////////////////////////////////////////////////////////////////////
template <typename Type> struct TFieldType;

template <> struct TFieldType<bool>			{ enum { Value = Field_Bool }; };
template <> struct TFieldType<int8>			{ enum { Value = Field_Int8 }; };
template <> struct TFieldType<int16>		{ enum { Value = Field_Int16 }; };
template <> struct TFieldType<int32>		{ enum { Value = Field_Int32 }; };
template <> struct TFieldType<int64>		{ enum { Value = Field_Int64 }; };
template <> struct TFieldType<uint8>		{ enum { Value = Field_Int8 }; };
template <> struct TFieldType<uint16>		{ enum { Value = Field_Int16 }; };
template <> struct TFieldType<uint32>		{ enum { Value = Field_Int32 }; };
template <> struct TFieldType<uint64>		{ enum { Value = Field_Int64 }; };
template <> struct TFieldType<float>		{ enum { Value = Field_Float }; };
template <> struct TFieldType<double>		{ enum { Value = Field_Double }; };
template <class T> struct TFieldType<T*>	{ enum { Value = Field_Ptr }; };

} // namespace Trace

#if UE_TRACE_ENABLED

#include "Templates/UnrealTemplate.h"

namespace Trace
{

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
template <int OffsetValue, typename Type>
struct TFieldBase
	: public FFieldDesc
{
	enum { Offset = OffsetValue, Size = sizeof(Type) };

protected:
	TFieldBase(const FLiteralName& Name)
	: FFieldDesc(Name, TFieldType<Type>::Value, OffsetValue, Size)
	{
	}
};

////////////////////////////////////////////////////////////////////////////////
template <int Offset, typename Type> struct TField;

////////////////////////////////////////////////////////////////////////////////
// Used to terminate the field list and determine an event's size.
enum EndOfFields {};
template <int Size>
struct TField<Size, EndOfFields>
{
	enum : uint16 { Value = Size };
};

////////////////////////////////////////////////////////////////////////////////
// Access to additional data that can be included along with a logged event.
enum Attachment {};
template <int Offset>
struct TField<Offset, Attachment>
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
template <int Offset, typename Type>
struct TField
	: public TFieldBase<Offset, Type>
{
	struct FActionable
	{
		Type Value;
		void Write(uint8* __restrict Ptr) const
		{
			*(Type*)(Ptr + Offset) = Value;
		}
	};

	TField(const FLiteralName& Name)
	: TFieldBase<Offset, Type>(Name)
	{
	}

	const FActionable operator () (const Type& __restrict Value) const
	{
		return { Value };
	}
};

} // namespace Trace

#endif // UE_TRACE_ENABLED
