// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

namespace Trace
{

#if defined(TRACE_PRIVATE_PROTOCOL_0)
inline
#endif
namespace Protocol0
{

////////////////////////////////////////////////////////////////////////////////
enum EProtocol : uint8 { Id = 0 };

////////////////////////////////////////////////////////////////////////////////
enum : uint8
{
	/* Category */
	Field_CategoryMask	= 0300,
	Field_Integer		= 0000, 
	Field_Float			= 0100,

	/* Size */
	Field_Pow2SizeMask	= 0003,
	Field_8				= 0000,
	Field_16			= 0001,
	Field_32			= 0002,
	Field_64			= 0003,
#if PLATFORM_64BITS
	Field_Ptr			= Field_64,
#else
	Field_Ptr			= Field_32,
#endif
};

////////////////////////////////////////////////////////////////////////////////
enum class EFieldType : uint8
{
	Bool	= Field_Integer | Field_8,
	Int8	= Field_Integer | Field_8,
	Int16	= Field_Integer | Field_16,
	Int32	= Field_Integer | Field_32,
	Int64	= Field_Integer | Field_64,
	Pointer	= Field_Integer | Field_Ptr,
	Float32	= Field_Float   | Field_32,
	Float64	= Field_Float   | Field_64,
};

////////////////////////////////////////////////////////////////////////////////
struct FNewEventEvent
{
	enum : uint16 { Uid = 0 };

	uint16		EventUid;
	uint16		FieldCount;
	uint8		LoggerNameSize;
	uint8		EventNameSize;
	struct
	{
		uint16	Offset;
		uint16	Size;
		uint8	TypeInfo;
		uint8	NameSize;
	}			Fields[];
	/*uint8		NameData[]*/
};

////////////////////////////////////////////////////////////////////////////////
struct FEventHeader
{
	uint16		Uid;
	uint16		Size;
	uint8		EventData[];
};

} // namespace Protocol0

} // namespace Trace
